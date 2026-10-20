// Maghreb Tubu - the voice.
//
// Additive, with a sine table and one phase accumulator per partial. That is
// a deliberate choice rather than the obvious one: a plucked-string delay
// line is cheaper and sounds more alive, but its pitch depends on a delay
// length that has to be an exact number of samples and on the group delay of
// its loop filter, so it lands a few cents off and the error changes with
// register. This instrument exists to place 150 and 350 cent degrees
// accurately. A tuning that is "about right" would defeat it, so the
// oscillator is exact by construction and the character comes from how the
// partials are weighted and decay instead.
//
// Framework-free, so the tuning can be measured offline.

#pragma once
#include "tubu_tuning.h"

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <atomic>

namespace tubu {

enum Param {
    kTab = 0, kTonic, kMapMode, kDrone, kDroneLevel,
    kBrightness, kDamping, kDecay, kBow, kAttack, kRelease,
    kLevel, kOutput,
    kEditDegree, kEditCents,
    kNumParams
};

enum DroneMode { droneOff = 0, droneTonic, droneTonicFifth, kNumDroneModes };

static const int kMaxVoices  = 8;
static const int kPartials   = 12;
static const int kSineBits   = 12;
static const int kSineSize   = 1 << kSineBits;    // 4096
static const int kSineMask   = kSineSize - 1;

// One shared table. Built once; every voice reads it.
inline const float* sineTable() {
    static std::vector<float> t;
    if (t.empty()) {
        t.resize(kSineSize + 1);
        for (int i = 0; i <= kSineSize; i++)
            t[i] = (float)std::sin(6.283185307179586 * i / kSineSize);
    }
    return t.data();
}

// What the engine is sounding, for the display.
struct VoiceView {
    float midi = -1.f;      // resolved pitch, fractional
    float level = 0.f;
};
struct EngineView {
    VoiceView voices[kMaxVoices];
    int   degreeCents[kDegrees] = {0};
    int   heldDegree[kDegrees] = {0};   // how many voices are on each degree
    int   tab = 0;
    int   neutral = 0;
    bool  edited = false;
    bool  droneOn = false;
};

class Tubu {
public:
    Tubu() { reset(); }

    void setSampleRate(double sr) {
        sr_ = (sr > 0) ? sr : 44100.0;
        reset();
    }
    double sampleRate() const { return sr_; }

    // ---------------- parameters ----------------

    void setParam(int id, float v) {
        if (id < 0 || id >= kNumParams) return;
        if (p_[id] == v) return;
        p_[id] = v;

        if (id == kTab) {
            tuning_.setTab((int)(v + 0.5f));
            retune();
        } else if (id == kTonic) {
            tuning_.setTonic((int)(v + 0.5f));
            retune();
        } else if (id == kMapMode) {
            tuning_.setMode((int)(v + 0.5f));
            retune();
        } else if (id == kEditCents) {
            tuning_.setDegreeCents(editDegree(), (int)(v + 0.5f));
            retune();
        } else if (id == kDrone) {
            refreshDrone();
        }
    }
    float getParam(int id) const {
        return (id >= 0 && id < kNumParams) ? p_[id] : 0.f;
    }

    int editDegree() const {
        return std::max(0, std::min(kDegrees - 1, (int)(p_[kEditDegree] + 0.5f)));
    }

    // Puts the factory tabʿ back and reports the cents so the wrapper can
    // tell the host what moved.
    void revertTuning(int* outCents /* kDegrees */) {
        tuning_.revert();
        if (outCents)
            for (int i = 0; i < kDegrees; i++) outCents[i] = tuning_.degreeCents(i);
        p_[kEditCents] = (float)tuning_.degreeCents(editDegree());
        retune();
    }

    const Tuning& tuning() const { return tuning_; }
    int  degreeCents(int d) const { return tuning_.degreeCents(d); }
    bool edited() const { return tuning_.edited(); }
    int  neutralCount() const { return tuning_.neutralCount(); }
    const char* tabName() const { return tuning_.name(); }

    // ---------------- notes ----------------

    void noteOn(int pitch, float vel) {
        // A key that resolves outside MIDI range is dropped, exactly as the
        // module does - per-step mapping can otherwise ask for a frequency
        // above the sample rate.
        if (!tuning_.playable(pitch)) return;

        Voice* slot = nullptr;
        for (int i = 0; i < kMaxVoices; i++)
            if (voices_[i].active && voices_[i].key == pitch) { slot = &voices_[i]; break; }
        if (!slot)
            for (int i = 0; i < kMaxVoices; i++)
                if (!voices_[i].active) { slot = &voices_[i]; break; }
        if (!slot) {
            slot = &voices_[0];
            for (int i = 1; i < kMaxVoices; i++)
                if (voices_[i].stamp < slot->stamp) slot = &voices_[i];
        }
        startVoice(*slot, pitch, vel, false);
    }

    void noteOff(int pitch) {
        for (int i = 0; i < kMaxVoices; i++)
            if (voices_[i].active && !voices_[i].drone && voices_[i].key == pitch)
                voices_[i].releasing = true;
    }

    void allNotesOff() {
        for (int i = 0; i < kMaxVoices; i++)
            if (!voices_[i].drone) voices_[i].releasing = true;
    }

    int activeVoices() const {
        int n = 0;
        for (int i = 0; i < kMaxVoices; i++)
            if (voices_[i].active && !voices_[i].drone) n++;
        return n;
    }

    // ---------------- render ----------------

    void process(float* L, float* R, int n) {
        for (int i = 0; i < n; i++) { L[i] = 0.f; R[i] = 0.f; }

        const double atkInc = 1.0 / std::max(1.0, (double)p_[kAttack] * sr_ * 0.001);
        const double relInc = 1.0 / std::max(1.0, (double)p_[kRelease] * sr_ * 0.001);
        const double bow = std::max(0.f, std::min(1.f, p_[kBow]));
        const float* tbl = sineTable();

        // Per-partial decay, recomputed per block. Higher partials fade
        // faster, which is what makes a plucked tone settle toward a sine
        // instead of staying buzzy.
        const double baseDecay = std::max(0.02, (double)p_[kDecay]);
        double partialDecay[kPartials];
        for (int k = 0; k < kPartials; k++) {
            const double mult = std::pow((double)(k + 1), (double)p_[kDamping]);
            const double tau = baseDecay / mult;
            // toward the sustain floor when bowed, toward silence when plucked
            partialDecay[k] = std::exp(-(double)n / (tau * sr_));
        }

        int live = 0;
        for (int i = 0; i < kMaxVoices; i++) if (voices_[i].active) live++;
        if (live == 0) { publish(); return; }
        const double norm = p_[kLevel] / std::sqrt((double)std::max(1, live));

        for (int vi = 0; vi < kMaxVoices; vi++) {
            Voice& v = voices_[vi];
            if (!v.active) continue;

            // envelope and per-partial amplitude move once per block
            for (int k = 0; k < kPartials; k++) {
                const double floorAmp = bow * v.partialStart[k];
                v.partialAmp[k] = floorAmp +
                    (v.partialAmp[k] - floorAmp) * partialDecay[k];
            }

            const double amp = v.vel * norm * (v.drone ? p_[kDroneLevel] : 1.0);
            for (int i = 0; i < n; i++) {
                if (v.releasing) v.env = std::max(0.0, v.env - relInc);
                else             v.env = std::min(1.0, v.env + atkInc);

                double s = 0.0;
                for (int k = 0; k < kPartials; k++) {
                    if (v.partialAmp[k] < 1e-5) continue;
                    v.phase[k] += v.inc[k];
                    if (v.phase[k] >= kSineSize) v.phase[k] -= kSineSize;
                    const int i0 = (int)v.phase[k];
                    const double f = v.phase[k] - i0;
                    s += (tbl[i0] * (1.0 - f) + tbl[i0 + 1] * f) * v.partialAmp[k];
                }
                s *= v.env * amp;
                L[i] += (float)(s * (1.0 - v.pan));
                R[i] += (float)(s * v.pan);
            }

            if (v.releasing && v.env <= 0.0) { v.active = false; v.key = -1; }
            // a plucked note that has fully decayed is finished too
            double loudest = 0;
            for (int k = 0; k < kPartials; k++) loudest = std::max(loudest, v.partialAmp[k]);
            if (!v.drone && loudest < 1e-5 && !v.releasing) { v.active = false; v.key = -1; }
        }

        const float g = dbGain(p_[kOutput]);
        if (g != 1.f) for (int i = 0; i < n; i++) { L[i] *= g; R[i] *= g; }
        publish();
    }

    const EngineView& snapshot() const {
        return views_[viewIdx_.load(std::memory_order_acquire)];
    }

private:
    struct Voice {
        bool active = false, releasing = false, drone = false;
        int  key = -1;
        double vel = 1.0, env = 0.0, pan = 0.5;
        double midi = 60.0;
        double phase[kPartials] = {0};
        double inc[kPartials] = {0};
        double partialAmp[kPartials] = {0};
        double partialStart[kPartials] = {0};
        uint64_t stamp = 0;
    };

    static float dbGain(float db) {
        if (db <= -59.9f) return 0.f;
        return (std::fabs(db) < 1e-6f) ? 1.f : std::pow(10.f, db / 20.f);
    }

    void startVoice(Voice& v, int pitch, float vel, bool drone) {
        v.active = true;
        v.releasing = false;
        v.drone = drone;
        v.key = pitch;
        v.vel = std::max(0.05f, vel);
        v.stamp = ++stamp_;
        v.env = 0.0;
        v.pan = 0.5;
        v.midi = drone ? pitch : tuning_.resolvedMidi(pitch);

        setPartials(v, 440.0 * std::pow(2.0, (v.midi - 69.0) / 12.0));
    }

    // Partials sum in phase at the attack, so a bright setting would peak at
    // the sum of their amplitudes - which is how a plain 1/k^b weighting ends
    // up clipping the host. Normalising by that sum keeps a voice inside
    // unity whatever the brightness.
    void setPartials(Voice& v, double freq) {
        const double bright = std::max(0.1f, p_[kBrightness]);
        double total = 0.0;
        for (int k = 0; k < kPartials; k++) {
            const double pf = freq * (k + 1);
            // never run a partial past Nyquist: it would fold back as an
            // inharmonic tone and the tuning would audibly stop being the
            // tuning
            v.inc[k] = (pf < sr_ * 0.45) ? (pf / sr_) * kSineSize : 0.0;
            v.phase[k] = 0.0;
            const double a = (v.inc[k] > 0.0)
                           ? 1.0 / std::pow((double)(k + 1), bright) : 0.0;
            v.partialStart[k] = a;
            total += a;
        }
        const double g = (total > 1e-9) ? 1.0 / total : 0.0;
        for (int k = 0; k < kPartials; k++) {
            v.partialStart[k] *= g;
            v.partialAmp[k] = v.partialStart[k];
        }
    }

    // The tuning moved under whatever is held, so drop it cleanly rather than
    // leave notes sounding at the old degrees.
    void retune() {
        for (int i = 0; i < kMaxVoices; i++)
            if (voices_[i].active && !voices_[i].drone) voices_[i].releasing = true;
        refreshDrone();
    }

    void refreshDrone() {
        for (int i = 0; i < kMaxVoices; i++)
            if (voices_[i].drone) { voices_[i].active = false; voices_[i].drone = false; }

        const int mode = (int)(p_[kDrone] + 0.5f);
        if (mode == droneOff) return;

        // The drone is placed straight from the tabʿ, so its fifth is the
        // tabʿ's fifth - 650 cents in Mujannab, not a plain 700.
        startDroneAt(tuning_.droneFrequency(false));
        if (mode == droneTonicFifth) startDroneAt(tuning_.droneFrequency(true));
    }

    void startDroneAt(double freq) {
        Voice* slot = nullptr;
        for (int i = kMaxVoices - 1; i >= 0; i--)
            if (!voices_[i].active) { slot = &voices_[i]; break; }
        if (!slot) return;

        startVoice(*slot, 0, 1.f, true);
        slot->midi = 69.0 + 12.0 * std::log2(freq / 440.0);
        setPartials(*slot, freq);      // straight from the tabʿ's frequency
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
        v.droneOn = (int)(p_[kDrone] + 0.5f) != droneOff;

        for (int i = 0; i < kMaxVoices; i++) {
            const Voice& s = voices_[i];
            v.voices[i].midi = s.active ? (float)s.midi : -1.f;
            v.voices[i].level = s.active ? (float)s.env : 0.f;
            if (!s.active || s.drone) continue;
            // which degree of the tabʿ this voice is sitting on
            const double c = std::fmod(s.midi - tuning_.tonic(), 12.0) * 100.0;
            for (int d = 0; d < kDegrees; d++)
                if (std::fabs(tuning_.degreeCents(d) - c) < 1.0) v.heldDegree[d]++;
        }
        viewIdx_.store(1 - cur, std::memory_order_release);
    }

    void reset() {
        for (int i = 0; i < kMaxVoices; i++) voices_[i] = Voice();
        stamp_ = 0;
        setDefaults();
        tuning_.setTab((int)p_[kTab]);
        tuning_.setTonic((int)p_[kTonic]);
        tuning_.setMode((int)p_[kMapMode]);
    }

    void setDefaults() {
        p_[kTab] = 3.f;              // Sika
        p_[kTonic] = 60.f;
        p_[kMapMode] = (float)mapPerStep;
        p_[kDrone] = (float)droneOff;
        p_[kDroneLevel] = 0.35f;
        p_[kBrightness] = 1.2f;
        p_[kDamping] = 1.0f;
        p_[kDecay] = 2.5f;           // seconds
        p_[kBow] = 0.f;              // plucked
        p_[kAttack] = 4.f;           // ms
        p_[kRelease] = 180.f;
        p_[kLevel] = 0.8f;
        p_[kOutput] = 0.f;
        p_[kEditDegree] = 1.f;
        p_[kEditCents] = 150.f;
    }

    double sr_ = 44100.0;
    float p_[kNumParams] = {0};
    Tuning tuning_;
    Voice voices_[kMaxVoices];
    uint64_t stamp_ = 0;

    EngineView views_[2];
    std::atomic<int> viewIdx_{0};
};

} // namespace tubu
