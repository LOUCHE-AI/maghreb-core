// Maghreb DX - the Maghreb FM kit, rendered by Dexed's engine.
//
// Same eleven strokes, same editable twelve numbers each, same notes 36-46 -
// but each strike is derived into a DX7 voice and played by msfa instead of
// by the module's two-operator engine. Editing a stroke here changes the
// derivation, so the controls mean what they mean in Maghreb FM even though
// nothing downstream of them is shared.
//
// msfa renders in fixed 64-sample blocks, so the host's buffer is served from
// a small residual rather than by asking the engine for an arbitrary length.

#pragma once
#include "dx_patch.h"
#include "dx_data.h"

#include "dx_engine.h"
#include "dx_sysex.h"

#include <cmath>
#include <cstring>
#include <cstdint>
#include <memory>
#include <vector>
#include <algorithm>
#include <atomic>

namespace mdx {

using mseq::StrokeDef;
using mseq::kKits;
using mseq::kNumKits;
using mseq::kStrokes;
using mseq::kStrokeNames;
using mseq::kStrokeChars;
using mseq::kNumStrokes;

static const int kMaxVoices = 16;

enum Param {
    kKit = 0, kTune, kDecay, kFM, kNoise, kTone, kDrive, kLevel,
    kHumanize, kSpread, kVelFM, kVelTone, kLowpass, kHighpass, kOutput,
    kSelect,
    kSFreq, kSRatio, kSIndex, kSFmDec, kSAmpDec, kSBend, kSBendDec,
    kSNoise, kSNoiseFreq, kSNoiseQ, kSNoiseDec, kSGain, kSPan,
    kNumParams
};
static const int kFirstStrokeParam = kSFreq;
static const int kNumStrokeParams = kSPan - kSFreq + 1;

struct EngineView {
    float strokeLevel[mseq::kNumStrokes] = {0};
    int select = 0;
    int edited = 0;
    int active = 0;
};

class MaghrebDX {
public:
    MaghrebDX() { reset(); }

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
        if (id >= kFirstStrokeParam && id <= kSPan) writeSelected(id);
    }
    float getParam(int id) const {
        return (id >= 0 && id < kNumParams) ? p_[id] : 0.f;
    }

    int selected() const {
        return clampi((int)(p_[kSelect] + 0.5f), 0, mseq::kNumStrokes - 1);
    }
    const StrokeDef& stroke(int i) const {
        return table_[clampi(i, 0, mseq::kNumStrokes - 1)];
    }
    float strokePan(int i) const { return pan_[clampi(i, 0, mseq::kNumStrokes - 1)]; }

    void loadSelectedInto(float* out) const {
        const StrokeDef& s = table_[selected()];
        out[kSFreq - kFirstStrokeParam] = s.freq;
        out[kSRatio - kFirstStrokeParam] = s.ratio;
        out[kSIndex - kFirstStrokeParam] = s.index;
        out[kSFmDec - kFirstStrokeParam] = s.fmDecay;
        out[kSAmpDec - kFirstStrokeParam] = s.ampDecay;
        out[kSBend - kFirstStrokeParam] = s.bend;
        out[kSBendDec - kFirstStrokeParam] = s.bendDecay;
        out[kSNoise - kFirstStrokeParam] = s.noise;
        out[kSNoiseFreq - kFirstStrokeParam] = s.noiseFreq;
        out[kSNoiseQ - kFirstStrokeParam] = s.noiseQ;
        out[kSNoiseDec - kFirstStrokeParam] = s.noiseDecay;
        out[kSGain - kFirstStrokeParam] = s.gain;
        out[kSPan - kFirstStrokeParam] = pan_[selected()];
    }

    void resetStroke(int i) {
        i = clampi(i, 0, mseq::kNumStrokes - 1);
        table_[i] = kStrokes[i];
        pan_[i] = 0.f;
    }
    void resetAll() { for (int i = 0; i < mseq::kNumStrokes; i++) resetStroke(i); }

    bool strokeEdited(int i) const {
        const StrokeDef& a = table_[clampi(i, 0, mseq::kNumStrokes - 1)];
        const StrokeDef& b = kStrokes[clampi(i, 0, mseq::kNumStrokes - 1)];
        return a.freq != b.freq || a.ratio != b.ratio || a.index != b.index ||
               a.fmDecay != b.fmDecay || a.ampDecay != b.ampDecay ||
               a.bend != b.bend || a.bendDecay != b.bendDecay ||
               a.noise != b.noise || a.noiseFreq != b.noiseFreq ||
               a.noiseQ != b.noiseQ || a.noiseDecay != b.noiseDecay ||
               a.gain != b.gain || pan_[i] != 0.f;
    }

    // ---------------- imported DX7 voices ----------------
    //
    // A pad either derives its patch from the stroke table, or plays a voice
    // lifted from a cartridge. The derivation is what makes it the Maghreb
    // kit; an imported voice is what makes it a DX7.
    int loadCartridge(const char* path) {
        std::vector<SysexVoice> v;
        const int n = mdx::loadSysex(path, v);
        if (n > 0) cart_ = v;
        return n;
    }
    int cartridgeSize() const { return (int)cart_.size(); }
    const char* cartridgeName(int i) const {
        return (i >= 0 && i < (int)cart_.size()) ? cart_[i].name : "";
    }

    bool assignVoice(int stroke, int voice) {
        if (stroke < 0 || stroke >= mseq::kNumStrokes) return false;
        if (voice < 0 || voice >= (int)cart_.size()) return false;
        std::memcpy(imported_[stroke], cart_[voice].data, 156);
        std::memcpy(importedName_[stroke], cart_[voice].name, 11);
        useImported_[stroke] = true;
        // A factory voice is much louder than a patch derived from a stroke -
        // measured over 1088 of them, the loudest peaks at 1.62 on its own.
        // Scale it once, here, so a pad does not clip the moment it is chosen.
        const float pk = engine_.patchPeak(imported_[stroke], kFirstNote + stroke);
        importedGain_[stroke] = (pk > 0.0001f) ? std::min(1.0f, 0.55f / pk) : 1.f;
        return true;
    }
    void clearVoice(int stroke) {
        if (stroke < 0 || stroke >= mseq::kNumStrokes) return;
        useImported_[stroke] = false;
        importedName_[stroke][0] = 0;
        importedGain_[stroke] = 1.f;
    }
    bool usesImported(int stroke) const {
        return stroke >= 0 && stroke < mseq::kNumStrokes && useImported_[stroke];
    }
    const char* importedName(int stroke) const {
        return (stroke >= 0 && stroke < mseq::kNumStrokes) ? importedName_[stroke] : "";
    }

    // the imported patches travel with the plug-in state
    static const int kImportFloats = mseq::kNumStrokes * 157;   // flag + 156
    void saveImported(float* out) const {
        int k = 0;
        for (int i = 0; i < mseq::kNumStrokes; i++) {
            out[k++] = useImported_[i] ? 1.f : 0.f;
            for (int b = 0; b < 156; b++) out[k++] = (float)imported_[i][b];
        }
    }
    void loadImported(const float* in) {
        int k = 0;
        for (int i = 0; i < mseq::kNumStrokes; i++) {
            useImported_[i] = in[k++] > 0.5f;
            for (int b = 0; b < 156; b++)
                imported_[i][b] = (uint8_t)clampi((int)(in[k++] + 0.5f), 0, 255);
            std::memcpy(importedName_[i], imported_[i] + 145, 10);
            importedName_[i][10] = 0;
            for (int c = 9; c >= 0 && (importedName_[i][c] == ' ' ||
                                       importedName_[i][c] == 0); c--)
                importedName_[i][c] = 0;
        }
    }

    static const int kTableFloats = mseq::kNumStrokes * 13;
    void saveTable(float* out) const {
        int k = 0;
        for (int i = 0; i < mseq::kNumStrokes; i++) {
            const StrokeDef& s = table_[i];
            out[k++] = s.freq; out[k++] = s.ratio; out[k++] = s.index;
            out[k++] = s.fmDecay; out[k++] = s.ampDecay; out[k++] = s.bend;
            out[k++] = s.bendDecay; out[k++] = s.noise; out[k++] = s.noiseFreq;
            out[k++] = s.noiseQ; out[k++] = s.noiseDecay; out[k++] = s.gain;
            out[k++] = pan_[i];
        }
    }
    void loadTable(const float* in) {
        int k = 0;
        for (int i = 0; i < mseq::kNumStrokes; i++) {
            StrokeDef& s = table_[i];
            s.freq = in[k++]; s.ratio = in[k++]; s.index = in[k++];
            s.fmDecay = in[k++]; s.ampDecay = in[k++]; s.bend = in[k++];
            s.bendDecay = in[k++]; s.noise = in[k++]; s.noiseFreq = in[k++];
            s.noiseQ = in[k++]; s.noiseDecay = in[k++]; s.gain = in[k++];
            pan_[i] = in[k++];
        }
    }

    // ---------------- notes ----------------
    void noteOn(int note, float vel) {
        const int stroke = note - kFirstNote;
        if (stroke < 0 || stroke >= mseq::kNumStrokes) return;
        trigger(stroke, vel);
    }
    void noteOff(int) {}
    void allNotesOff() { engine_.allOff(); }

    void queueTrigger(int stroke) {
        if (stroke < 0 || stroke >= mseq::kNumStrokes) return;
        pending_.fetch_or(1u << stroke, std::memory_order_release);
    }

    void trigger(int stroke, float vel) {
        startVoice(clampi(stroke, 0, mseq::kNumStrokes - 1), vel);
    }

    int activeVoices() const { return engine_.activeVoices(); }

    // ---------------- render ----------------
    void process(float* L, float* R, int n) {
        const uint32_t due = pending_.exchange(0, std::memory_order_acquire);
        if (due)
            for (int i = 0; i < mseq::kNumStrokes; i++)
                if (due & (1u << i)) trigger(i, 1.f);

        for (int i = 0; i < n; i++) { L[i] = 0.f; R[i] = 0.f; }
        engine_.render(L, R, n);
        engine_.takeStrokePeaks(strokeLevel_, mseq::kNumStrokes);

        filterBlock(L, R, n);
        const float g = dbGain(p_[kOutput]);
        if (g != 1.f) for (int i = 0; i < n; i++) { L[i] *= g; R[i] *= g; }
        publish();
    }

    const EngineView& snapshot() const {
        return views_[viewIdx_.load(std::memory_order_acquire)];
    }

private:
    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float dbGain(float db) {
        if (db <= -59.9f) return 0.f;
        return (std::fabs(db) < 1e-6f) ? 1.f : std::pow(10.f, db / 20.f);
    }
    double rnd() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return (double)(int32_t)rng_ / 2147483648.0;
    }


    void writeSelected(int id) {
        StrokeDef& s = table_[selected()];
        const float v = p_[id];
        switch (id) {
        case kSFreq: s.freq = v; break;
        case kSRatio: s.ratio = v; break;
        case kSIndex: s.index = v; break;
        case kSFmDec: s.fmDecay = v; break;
        case kSAmpDec: s.ampDecay = v; break;
        case kSBend: s.bend = v; break;
        case kSBendDec: s.bendDecay = v; break;
        case kSNoise: s.noise = v; break;
        case kSNoiseFreq: s.noiseFreq = v; break;
        case kSNoiseQ: s.noiseQ = v; break;
        case kSNoiseDec: s.noiseDecay = v; break;
        case kSGain: s.gain = v; break;
        case kSPan: pan_[selected()] = v; break;
        default: break;
        }
    }

    void startVoice(int stroke, float vel) {
        vel = clampf(vel, 0.f, 1.f);
        const double h = p_[kHumanize];
        const double jitterTune = rnd() * h * 1.5;
        const double jitterLvl = 1.0 + rnd() * h * 0.35;

        const int kit = clampi((int)(p_[kKit] + 0.5f), 0, kNumKits - 1);
        DeriveSettings d;
        d.kitFreq = kKits[kit].freq;
        d.kitDecay = kKits[kit].decay;
        d.kitNoise = kKits[kit].noise;
        d.kitIndex = kKits[kit].index;
        d.tune = p_[kTune] + (float)jitterTune;
        d.decay = p_[kDecay];
        d.fmAmount = p_[kFM];
        d.noiseAmount = p_[kNoise];
        d.tone = p_[kTone];
        d.velocity = vel;
        d.velFM = p_[kVelFM];

        // Derived per strike rather than cached: it is a few hundred
        // arithmetic ops, and it means an edit is heard on the very next hit
        // without any invalidation to get wrong.
        uint8_t patch[156];
        float norm = 1.f;
        if (useImported_[stroke]) {
            // An imported voice is played as it is. Tune, Decay and the rest
            // are derivation controls and have nothing to reach into here,
            // which is worth knowing rather than discovering.
            std::memcpy(patch, imported_[stroke], 156);
            norm = importedGain_[stroke];
        } else {
            derivePatch(table_[stroke], stroke, d, patch);
        }

        const double base = pan_[stroke];
        const float pan = clampf((float)(0.5 + base * 0.5 * p_[kSpread]), 0.f, 1.f);
        const int mv = clampi((int)(vel * 99.f + 0.5f), 1, 99);
        engine_.noteOn(patch, kFirstNote + stroke, mv, pan,
                       (float)(p_[kLevel] * jitterLvl) * norm, stroke);
    }

    void filterBlock(float* L, float* R, int n) {
        const double lpHz = std::min((double)p_[kLowpass], sr_ * 0.45);
        const double hpHz = std::max(5.0, (double)p_[kHighpass]);
        const double alp = 1.0 - std::exp(-6.283185307179586 * lpHz / sr_);
        const double ahp = 1.0 - std::exp(-6.283185307179586 * hpHz / sr_);
        const bool doLp = lpHz < sr_ * 0.44;
        const bool doHp = hpHz > 6.0;
        const double drive = 1.0 + p_[kDrive] * 4.0;
        const double makeup = 1.0 / (1.0 + p_[kDrive] * 1.2);
        const bool doDrive = p_[kDrive] > 0.0001f;
        if (!doLp && !doHp && !doDrive) return;

        for (int i = 0; i < n; i++) {
            double l = L[i], r = R[i];
            if (doLp) { lpL_ += alp * (l - lpL_); lpR_ += alp * (r - lpR_); l = lpL_; r = lpR_; }
            if (doHp) { hpL_ += ahp * (l - hpL_); hpR_ += ahp * (r - hpR_); l -= hpL_; r -= hpR_; }
            if (doDrive) {
                l = std::tanh(l * drive) * makeup;
                r = std::tanh(r * drive) * makeup;
            }
            L[i] = (float)l; R[i] = (float)r;
        }
    }

    void publish() {
        const int cur = viewIdx_.load(std::memory_order_relaxed);
        EngineView& v = views_[1 - cur];
        for (int i = 0; i < mseq::kNumStrokes; i++) v.strokeLevel[i] = strokeLevel_[i];
        v.select = selected();
        v.active = activeVoices();
        int ed = 0;
        for (int i = 0; i < mseq::kNumStrokes; i++) if (strokeEdited(i)) ed++;
        v.edited = ed;
        viewIdx_.store(1 - cur, std::memory_order_release);
    }

    void reset() {
        engine_.allOff();
        lpL_ = lpR_ = hpL_ = hpR_ = 0;
        rng_ = 0x2545F491;
        setDefaults();
        resetAll();
    }

    void setDefaults() {
        p_[kKit] = 0.f; p_[kTune] = 0.f; p_[kDecay] = 1.f;
        p_[kFM] = 1.f; p_[kNoise] = 1.f; p_[kTone] = 0.5f;
        p_[kDrive] = 0.12f; p_[kLevel] = 0.9f;
        p_[kHumanize] = 0.3f; p_[kSpread] = 0.6f;
        p_[kVelFM] = 0.55f; p_[kVelTone] = 0.45f;
        p_[kLowpass] = 18000.f; p_[kHighpass] = 5.f;
        p_[kOutput] = 0.f; p_[kSelect] = 0.f;
        const StrokeDef& s = kStrokes[0];
        p_[kSFreq] = s.freq; p_[kSRatio] = s.ratio; p_[kSIndex] = s.index;
        p_[kSFmDec] = s.fmDecay; p_[kSAmpDec] = s.ampDecay; p_[kSBend] = s.bend;
        p_[kSBendDec] = s.bendDecay; p_[kSNoise] = s.noise;
        p_[kSNoiseFreq] = s.noiseFreq; p_[kSNoiseQ] = s.noiseQ;
        p_[kSNoiseDec] = s.noiseDecay; p_[kSGain] = s.gain; p_[kSPan] = 0.f;
    }

    double sr_ = 44100.0;
    float p_[kNumParams] = {0};
    StrokeDef table_[mseq::kNumStrokes];
    float pan_[mseq::kNumStrokes] = {0};

    DxEngine engine_;
    float strokeLevel_[mseq::kNumStrokes] = {0};

    std::vector<SysexVoice> cart_;
    bool useImported_[mseq::kNumStrokes] = {false};
    uint8_t imported_[mseq::kNumStrokes][156] = {{0}};
    float importedGain_[mseq::kNumStrokes] = {0};
    char importedName_[mseq::kNumStrokes][11] = {{0}};

    double lpL_ = 0, lpR_ = 0, hpL_ = 0, hpR_ = 0;
    uint32_t rng_ = 0x2545F491;

    std::atomic<uint32_t> pending_{0};
    EngineView views_[2];
    std::atomic<int> viewIdx_{0};
};

} // namespace mdx
