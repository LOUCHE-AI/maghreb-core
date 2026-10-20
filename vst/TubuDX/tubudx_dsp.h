// Tubu DX - the eighteen ṭubūʿ played by Dexed's engine.
//
// The Maghreb Tubu VST3 places neutral degrees with an additive oscillator,
// which is exact but plain. The Move module and the Max device place them
// with pitch bend, which is per-channel and so cannot hold two at once. This
// does neither: msfa asks its TuningState for a log-frequency per MIDI note
// and uses the answer for every operator of every voice, so filling that
// table from a tabʿ puts six-operator FM exactly on the degrees - polyphonic,
// no bend anywhere, and any DX7 patch you like on top.
//
// Framework-free apart from the engine wall, so the tuning can be measured.

#pragma once
#include "tubu_tuning.h"
#include "dx_engine.h"
#include "dx_sysex.h"
#include "soft_clip.h"

#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <atomic>

namespace tdx {

using tubu::kDegrees;
using tubu::kNumTubu;
using tubu::kNames;
// the engine wall and the cartridge reader are shared with Maghreb DX
using mdx::DxEngine;

static const int kMaxVoices = 16;

enum Param {
    kTab = 0, kTonic, kMapMode,
    kEditDegree, kEditCents,
    kDrone, kDroneLevel,
    kVoice, kLevel, kOutput,
    kNumParams
};

struct EngineView {
    int   degreeCents[kDegrees] = {0};
    int   heldDegree[kDegrees] = {0};
    int   tab = 0;
    int   neutral = 0;
    bool  edited = false;
    bool  droneOn = false;
    int   active = 0;
    int   cartSize = 0;
    int   voice = 0;
};

class TubuDX {
public:
    TubuDX() { reset(); }

    void setSampleRate(double sr) {
        sr_ = (sr > 0) ? sr : 44100.0;
        engine_.setSampleRate(sr_);
        reset();
    }
    double sampleRate() const { return sr_; }

    void setParam(int id, float v) {
        if (id < 0 || id >= kNumParams) return;
        if (p_[id] == v) return;
        p_[id] = v;

        // Choosing a different voice means a different patch, so an edit made
        // on the previous one is dropped rather than carried across.
        if (id == kVoice) revertPatch();

        if (id == kTab)          { tuning_.setTab((int)(v + 0.5f)); retune(); }
        else if (id == kTonic)   { tuning_.setTonic((int)(v + 0.5f)); retune(); }
        else if (id == kMapMode) { tuning_.setMode((int)(v + 0.5f)); retune(); }
        else if (id == kEditCents) {
            tuning_.setDegreeCents(editDegree(), (int)(v + 0.5f));
            retune();
        }
        else if (id == kDrone)   refreshDrone();
    }
    float getParam(int id) const {
        return (id >= 0 && id < kNumParams) ? p_[id] : 0.f;
    }

    int editDegree() const {
        return std::max(0, std::min(kDegrees - 1, (int)(p_[kEditDegree] + 0.5f)));
    }
    const tubu::Tuning& tuning() const { return tuning_; }
    int  degreeCents(int d) const { return tuning_.degreeCents(d); }
    bool edited() const { return tuning_.edited(); }
    const char* tabName() const { return tuning_.name(); }

    void revertTuning(int* out) {
        tuning_.revert();
        if (out) for (int i = 0; i < kDegrees; i++) out[i] = tuning_.degreeCents(i);
        p_[kEditCents] = (float)tuning_.degreeCents(editDegree());
        retune();
    }

    // ---------------- cartridge ----------------
    int loadCartridge(const char* path) {
        std::vector<mdx::SysexVoice> v;
        const int n = mdx::loadSysex(path, v);
        if (n <= 0) return n;
        cart_ = v;
        edited_ = false;
        // Factory voices vary hugely in level - measured over the 1088 in the
        // folder here, from 0.05 to 1.62 peak on one note - and this is a
        // polyphonic instrument, so a loud one held as a chord clips. Each
        // voice is measured once, at load, and scaled. Thirty-two short
        // renders, which is a few tens of milliseconds.
        cartGain_.assign(cart_.size(), 1.f);
        for (size_t i = 0; i < cart_.size(); i++) {
            const float pk = engine_.patchPeak(cart_[i].data, tuning_.tonic());
            cartGain_[i] = (pk > 0.0001f) ? std::min(1.0f, 0.5f / pk) : 1.f;
        }
        return n;
    }

    // The scaling for whatever voice is selected, or 1 for the built-in patch.
    float voiceGain() const {
        if (edited_) return editedGain_;
        if (cart_.empty()) return 1.f;
        const int i = voiceIndex();
        return (i >= 0 && i < (int)cartGain_.size()) ? cartGain_[i] : 1.f;
    }
    int cartridgeSize() const { return (int)cart_.size(); }
    const char* voiceName(int i) const {
        if (cart_.empty()) return "built-in";
        return (i >= 0 && i < (int)cart_.size()) ? cart_[i].name : "";
    }
    // The 156 bytes that are actually playing: the loaded cartridge voice, or
    // the built-in patch when no cartridge is loaded. The patch page draws
    // these, so it is showing the sound rather than a guess at it.
    void currentPatch(uint8_t out[156]) const {
        const uint8_t* p = edited_ ? editedPatch_
                                   : (cart_.empty() ? defaultPatch_
                                                    : cart_[voiceIndex()].data);
        for (int i = 0; i < 156; i++) out[i] = p[i];
    }

    // ---- editing the voice ----
    //
    // The bytes being played are a cartridge's, so the first edit takes a copy
    // and the instrument plays that instead. Choosing a different voice drops
    // the copy: the edit belonged to the patch it was made on, and silently
    // carrying it onto the next one would be worse than losing it.
    bool patchEdited() const { return edited_; }

    void setPatchByte(int off, int value, int lo, int hi) {
        if (off < 0 || off >= 156) return;
        if (!edited_) { currentPatch(editedPatch_); edited_ = true; }
        editedPatch_[off] = (uint8_t)std::max(lo, std::min(hi, value));
        const float pk = engine_.patchPeak(editedPatch_, tuning_.tonic());
        editedGain_ = (pk > 0.0001f) ? std::min(1.0f, 0.5f / pk) : 1.f;
    }
    void revertPatch() { edited_ = false; editedGain_ = 1.f; }

    const uint8_t* editedBytes() const { return editedPatch_; }
    void loadEdited(const uint8_t* b) {
        if (!b) return;
        for (int i = 0; i < 156; i++) editedPatch_[i] = b[i];
        edited_ = true;
        const float pk = engine_.patchPeak(editedPatch_, tuning_.tonic());
        editedGain_ = (pk > 0.0001f) ? std::min(1.0f, 0.5f / pk) : 1.f;
    }
    bool operatorWave(int op, float* out, int n) const {
        uint8_t patch[156];
        currentPatch(patch);
        return engine_.renderOperator(patch, op, tuning_.tonic() + 12, out, n);
    }

    int voiceIndex() const {
        if (cart_.empty()) return 0;
        return std::max(0, std::min((int)cart_.size() - 1, (int)(p_[kVoice] + 0.5f)));
    }

    // ---------------- notes ----------------
    void noteOn(int note, float vel) {
        // A key that resolves outside MIDI range is dropped, as the module
        // does: per-step mapping can otherwise ask for an absurd frequency.
        if (!tuning_.playable(note)) return;
        uint8_t patchBuf[156];
        currentPatch(patchBuf);
        const uint8_t* patch = patchBuf;
        const int mv = std::max(1, std::min(99, (int)(vel * 99.f + 0.5f)));
        engine_.noteOn(patch, note, mv, 0.5f, p_[kLevel] * voiceGain(), note);
        held_[note & 127] = true;
    }
    void noteOff(int note) {
        engine_.noteOff(note);
        held_[note & 127] = false;
    }
    void allNotesOff() {
        engine_.allOff();
        std::memset(held_, 0, sizeof held_);
        droneOn_ = false;
    }
    int activeVoices() const { return engine_.activeVoices(); }

    // ---------------- render ----------------
    void process(float* L, float* R, int n) {
        for (int i = 0; i < n; i++) { L[i] = 0.f; R[i] = 0.f; }
        engine_.render(L, R, n);
        const float g = dbGain(p_[kOutput]);
        if (g != 1.f) for (int i = 0; i < n; i++) { L[i] *= g; R[i] *= g; }
        // Six voices of a normalised patch still sum past 1.0; see soft_clip.h.
        mdsp::softClipBlock(L, R, n);
        publish();
    }

    const EngineView& snapshot() const {
        return views_[viewIdx_.load(std::memory_order_acquire)];
    }

private:
    static float dbGain(float db) {
        if (db <= -59.9f) return 0.f;
        return (std::fabs(db) < 1e-6f) ? 1.f : std::pow(10.f, db / 20.f);
    }

    // The whole point: every MIDI note gets the tabʿ's own frequency, in the
    // fixed-point log units msfa works in.
    void rebuildTable() {
        for (int n = 0; n < 128; n++) {
            double hz = tuning_.frequencyFor(n);
            if (!std::isfinite(hz) || hz < 8.0) hz = 8.0;
            if (hz > 16000.0) hz = 16000.0;
            table_[n] = (int32_t)std::llround(std::log2(hz) * (double)(1 << 24));
        }
        engine_.setTuningTable(table_);
    }

    // The tuning moved under whatever is held, so drop it rather than leave
    // notes sounding on degrees that no longer exist.
    void retune() {
        rebuildTable();
        engine_.allOff();
        std::memset(held_, 0, sizeof held_);
        droneOn_ = false;
        refreshDrone();
    }

    // Which key sounds a given degree, in the octave above the tonic. The
    // answer differs per mapping, so it is searched rather than assumed.
    int keyForDegree(int degree) const {
        if (degree < 0 || degree >= kDegrees) return -1;
        const double want = tuning_.degreeCents(degree);
        for (int n = tuning_.tonic(); n <= tuning_.tonic() + 24 && n < 128; n++) {
            if (!tuning_.playable(n)) continue;
            if (std::fabs(tuning_.centsFor(n) - want) < 1.0) return n;
        }
        return -1;
    }

    void refreshDrone() {
        const int mode = (int)(p_[kDrone] + 0.5f);
        if (droneOn_) { engine_.allOff(); droneOn_ = false; }
        if (mode == 0) return;

        // The drone is placed from the tabʿ, so its fifth is the tabʿ's fifth.
        uint8_t patchBuf[156];
        currentPatch(patchBuf);
        const uint8_t* patch = patchBuf;
        const int tonic = tuning_.tonic();
        engine_.noteOn(patch, tonic, 70, 0.5f,
                       p_[kLevel] * p_[kDroneLevel] * voiceGain(), tonic);
        if (mode == 2) {
            // The fifth has to be asked for as a *key*, not as a pitch.
            // Rounding 650 cents to seven semitones and playing that key gets
            // it twice wrong: once in the rounding, and again because the key
            // is then remapped by the tuning table and lands somewhere else
            // entirely. So find the key that actually resolves to the fifth
            // degree, whichever mapping is in force.
            const int key = keyForDegree(4);
            if (key >= 0)
                engine_.noteOn(patch, key, 70, 0.5f,
                               p_[kLevel] * p_[kDroneLevel] * voiceGain(), key);
        }
        droneOn_ = true;
    }

    void publish() {
        const int cur = viewIdx_.load(std::memory_order_relaxed);
        EngineView& v = views_[1 - cur];
        for (int i = 0; i < kDegrees; i++) {
            v.degreeCents[i] = tuning_.degreeCents(i);
            v.heldDegree[i] = 0;
        }
        v.tab = tuning_.tab();
        v.neutral = tuning_.neutralCount();
        v.edited = tuning_.edited();
        v.droneOn = (int)(p_[kDrone] + 0.5f) != 0;
        v.active = engine_.activeVoices();
        v.cartSize = (int)cart_.size();
        v.voice = voiceIndex();

        // which degrees are under the fingers right now
        for (int n = 0; n < 128; n++) {
            if (!held_[n]) continue;
            const double c = std::fmod(tuning_.centsFor(n), 1200.0);
            for (int d = 0; d < kDegrees; d++)
                if (std::fabs(tuning_.degreeCents(d) - c) < 1.0) v.heldDegree[d]++;
        }
        viewIdx_.store(1 - cur, std::memory_order_release);
    }

    void reset() {
        setDefaults();
        tuning_.setTab((int)p_[kTab]);
        tuning_.setTonic((int)p_[kTonic]);
        tuning_.setMode((int)p_[kMapMode]);
        buildDefaultPatch();
        std::memset(held_, 0, sizeof held_);
        droneOn_ = false;
        rebuildTable();
    }

    void setDefaults() {
        p_[kTab] = 3.f;                       // Sika
        p_[kTonic] = 60.f;
        p_[kMapMode] = (float)tubu::mapPerStep;
        p_[kEditDegree] = 1.f;
        p_[kEditCents] = 150.f;
        p_[kDrone] = 0.f;
        p_[kDroneLevel] = 0.35f;
        p_[kVoice] = 0.f;
        p_[kLevel] = 0.8f;
        p_[kOutput] = 0.f;
    }

    static void setOp(uint8_t* p, int op, int r1, int r2, int r3, int r4,
                      int l1, int l2, int l3, int l4, int outLevel,
                      int mode, int coarse, int fine, int detune) {
        uint8_t* o = p + (5 - op) * 21;
        o[0] = (uint8_t)r1; o[1] = (uint8_t)r2; o[2] = (uint8_t)r3; o[3] = (uint8_t)r4;
        o[4] = (uint8_t)l1; o[5] = (uint8_t)l2; o[6] = (uint8_t)l3; o[7] = (uint8_t)l4;
        for (int i = 8; i < 16; i++) o[i] = 0;
        o[16] = (uint8_t)outLevel;
        o[17] = (uint8_t)mode; o[18] = (uint8_t)coarse; o[19] = (uint8_t)fine;
        o[20] = (uint8_t)(detune + 7);
    }

    // Something to hear before a cartridge is loaded: a plucked two-operator
    // tone, held while the key is down. Deliberately plain - the tuning is
    // the subject, and a factory patch would be arguing with it.
    void buildDefaultPatch() {
        uint8_t* p = defaultPatch_;
        for (int i = 0; i < 156; i++) p[i] = 0;
        for (int op = 0; op < 6; op++)
            setOp(p, op, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 1, 0, 0);
        // carrier: quick attack, long sustain, moderate release
        setOp(p, 0, 96, 40, 30, 55, 99, 92, 88, 0, 99, 0, 1, 0, 0);
        // modulator an octave up, falling away so the attack is brighter
        setOp(p, 1, 96, 55, 40, 60, 88, 55, 40, 0, 74, 0, 2, 0, 1);
        p[126] = 99; p[127] = 99; p[128] = 99; p[129] = 99;
        p[130] = 50; p[131] = 50; p[132] = 50; p[133] = 50;
        p[134] = 0;      // algorithm 1
        p[135] = 3;      // a little feedback for body
        p[136] = 1;      // key sync
        p[137] = 35;
        p[144] = 24;     // no transpose
        const char* nm = "TUBU      ";
        for (int i = 0; i < 10; i++) p[145 + i] = (uint8_t)nm[i];
    }

    double sr_ = 44100.0;
    float p_[kNumParams] = {0};
    tubu::Tuning tuning_;
    DxEngine engine_;

    int32_t table_[128] = {0};
    uint8_t defaultPatch_[156] = {0};
    std::vector<mdx::SysexVoice> cart_;
    uint8_t editedPatch_[156] = {0};
    bool edited_ = false;
    float editedGain_ = 1.f;
    std::vector<float> cartGain_;
    bool held_[128] = {false};
    bool droneOn_ = false;

    EngineView views_[2];
    std::atomic<int> viewIdx_{0};
};

} // namespace tdx
