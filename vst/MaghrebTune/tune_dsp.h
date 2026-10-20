// Maghreb Tune - the effect.
//
// One voice in, the same voice on a degree of the ṭabʿ out. The chain is
// short and each link was measured on its own before it was joined:
//
//   detector  ->  nearest degree  ->  corrector  ->  PSOLA shifter
//
// The detector runs on a hop rather than per sample, because pitch is a
// property of a window and asking for it more often than the window slides
// buys nothing. Everything downstream is updated at that rate; the shifter
// itself runs continuously.
//
// Framework-free, so the whole path can be driven with a synthesised singer
// and the output pitch measured.

#pragma once
#include "tune_detect.h"
#include "tune_scale.h"
#include "tune_shift.h"

#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>

namespace mtune {

enum Param {
    kTab = 0,        // which of the eighteen
    kTonic,          // MIDI note of the tonic
    kTonicFine,      // cents, so the grid can meet a singer who is not at A440
    kAmount,         // how far towards the degree
    kRetune,         // ms; 0 is the hard snap raï uses
    kDeadzone,       // cents of lean left alone
    kMix,            // dry against corrected
    kOutput,         // dB
    kBypass,
    kSensitivity,    // appended: how readily the detector calls something a note
    kLive,           // appended: shorter grain, less delay, no low voices
    kNumParams
};

// What the editor draws.
struct EngineView {
    double inHz = 0.0;        // what was sung
    double targetHz = 0.0;    // the degree it is being put on
    double offCents = 0.0;    // how far off it was
    double moveCents = 0.0;   // how far it is actually being moved
    int degree = -1;
    double confidence = 0.0;
    bool voiced = false;
    int tab = 0;
};

class MaghrebTune {
public:
    // The lowest note the detector looks for. The shifter sizes its grain -
    // and therefore the latency - from the same number.
    static constexpr double kLowestHz = 70.0;
    // What Live drops the floor to. A male voice at A2 is 110 Hz, so 100 still
    // carries a low raï lead; a bass below G#2 is what is given up for the
    // eight milliseconds.
    static constexpr double kLiveLowestHz = 100.0;
    // Amount may be pushed past a hundred per cent, so the clamp is not 1.
    static constexpr float kMaxAmount = 1.5f;
    static const int kHop = 256;
    static const int kWindow = 2048;

    MaghrebTune() { setDefaults(); }

    void setSampleRate(double sr) {
        sr_ = (sr > 0) ? sr : 44100.0;
        det_.setup(sr_, kWindow);
        corr_.setup(sr_, kHop);
        applySensitivity();
        // Set up for the longest grain either mode can ask for, then narrow to
        // whichever is selected: setLowest only moves the window into buffers
        // that already exist.
        shift_.setup(sr_, kLowestHz);
        shiftR_.setup(sr_, kLowestHz);
        applyRange();
        ring_.assign(kWindow * 4, 0.f);
        work_.assign(kWindow, 0.f);
        dry_.assign(8192, 0.f);
        dryR_.assign(8192, 0.f);
        mono_.assign(4096, 0.f);
        wpos_ = 0;
        sinceHop_ = 0;
        reset();
    }

    void setParam(int id, float v) {
        if (id < 0 || id >= kNumParams) return;
        p_[id] = v;
        if (id == kTab) tuning_.setTab((int)(v + 0.5f));
        if (id == kSensitivity) applySensitivity();
        if (id == kLive) applyRange();
    }
    float getParam(int id) const { return (id >= 0 && id < kNumParams) ? p_[id] : 0.f; }

    // The degrees are editable, as they are everywhere else in the family.
    void setDegreeCents(int d, int cents) { tuning_.setDegreeCents(d, cents); }
    int  degreeCents(int d) const { return tuning_.degreeCents(d); }
    void revertTuning() { tuning_.revert(); }
    const char* tabName() const { return tuning_.name(); }
    int  neutralCount() const { return tuning_.neutralCount(); }

    void reset() {
        corr_.reset();
        shift_.reset();
        shiftR_.reset();
        std::fill(ring_.begin(), ring_.end(), 0.f);
        std::fill(dry_.begin(), dry_.end(), 0.f);
        std::fill(dryR_.begin(), dryR_.end(), 0.f);
        dpos_ = 0; dposR_ = 0;
    }

    int latency() const { return shift_.latency(); }

    void process(const float* in, float* out, int n) {
        if (ring_.empty() || n <= 0) return;

        if (p_[kBypass] >= 0.5f) {
            for (int i = 0; i < n; i++) out[i] = in[i];
            return;
        }

        analyseBlock(in, n);
        shift_.process(in, out, n);
        mixInto(in, out, n, dry_, dpos_);
        publish();
    }

    // Stereo, driven by one decision.
    //
    // Pitch is detected on the sum and both channels are shifted by the same
    // ratio. Running two independent detectors on a doubled vocal would let
    // the channels disagree about which degree is meant and pull them apart.
    void processStereo(const float* inL, const float* inR,
                       float* outL, float* outR, int n) {
        if (n <= 0) return;
        if (p_[kBypass] >= 0.5f) {
            for (int i = 0; i < n; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
            return;
        }
        if (mono_.size() < (size_t)n) mono_.resize(n);
        for (int i = 0; i < n; i++) mono_[i] = 0.5f * (inL[i] + inR[i]);

        analyseBlock(mono_.data(), n);

        shift_.process(inL, outL, n);
        shiftR_.setPeriod(shift_.periodNow());
        shiftR_.setRatio(shift_.ratio());
        shiftR_.process(inR, outR, n);

        mixInto(inL, outL, n, dry_, dpos_);
        mixInto(inR, outR, n, dryR_, dposR_);
        publish();
    }

    const EngineView& snapshot() const {
        return views_[idx_.load(std::memory_order_acquire)];
    }

private:
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float dbGain(float db) {
        if (db <= -59.9f) return 0.f;
        return (std::fabs(db) < 1e-6f) ? 1.f : std::pow(10.f, db / 20.f);
    }

    // One control over both thresholds. Down is strict - it will only call
    // something a note when it is clearly periodic, which suits a close, dry
    // vocal. Up is forgiving, for a breathy or distant one, at the cost of
    // occasionally finding a pitch in something that is not really a note.
    void applySensitivity() {
        const double s = clampf(p_[kSensitivity], 0.f, 1.f);
        det_.setThreshold(0.08 + 0.14 * s);
        det_.setHold((int)(10 + 60 * s));
    }

    // Live is the whole of the latency question, in one place.
    //
    // The detector and the shifter have to agree on the lowest note: a
    // detector reporting 80 Hz to a shifter whose grain stops at 100 would
    // have its period refused and would go on cutting at the last rate it
    // believed, which is worse than not following the note at all.
    void applyRange() {
        const double lowest = (p_[kLive] >= 0.5f) ? kLiveLowestHz : kLowestHz;
        det_.setRange(lowest, 1100.0);
        shift_.setLowest(lowest);
        shiftR_.setLowest(lowest);
    }

    void analyseBlock(const float* in, int n) {
        for (int i = 0; i < n; i++)
            ring_[(wpos_ + i) % (int)ring_.size()] = in[i];
        wpos_ = (wpos_ + n) % (int)ring_.size();
        sinceHop_ += n;
        while (sinceHop_ >= kHop) {
            sinceHop_ -= kHop;
            analyse();
        }
    }

    // Dry, delayed to meet the shifted signal, then mixed. Mixing an undelayed
    // dry against a shifted wet is comb filtering with extra steps.
    void mixInto(const float* in, float* out, int n,
                 std::vector<float>& line, int& pos) {
        const int lat = shift_.latency();
        const float mix = clampf(p_[kMix], 0.f, 1.f);
        const float g = dbGain(p_[kOutput]);
        const int dsize = (int)line.size();
        for (int i = 0; i < n; i++) {
            line[pos] = in[i];
            const int at = (pos - lat + dsize * 2) % dsize;
            out[i] = (line[at] * (1.f - mix) + out[i] * mix) * g;
            pos = (pos + 1) % dsize;
        }
    }

    void analyse() {
        // the most recent window, unwrapped
        const int size = (int)ring_.size();
        int at = (wpos_ - kWindow + size * 2) % size;
        for (int i = 0; i < kWindow; i++) { work_[i] = ring_[at]; at = (at + 1) % size; }

        const Pitch p = det_.analyse(work_.data(), kWindow);
        inHz_ = p.hz;
        conf_ = p.confidence;
        voiced_ = p.voiced;

        if (!p.voiced || p.hz <= 0.0) {
            // Hold the last correction through a consonant rather than
            // letting the voice snap back and forth around it.
            corr_.setAmount(clampf(p_[kAmount], 0.f, kMaxAmount));
            corr_.setGlide(std::max(0.f, p_[kRetune]));
            corr_.setDeadzone(std::max(0.f, p_[kDeadzone]));
            shift_.setRatio(corr_.ratioFor(0.0, 0.0));
            targetHz_ = 0.0;
            degree_ = -1;
            return;
        }

        const double tonic = p_[kTonic] + p_[kTonicFine] / 100.0;
        const Target t = nearestDegree(tuning_, p.hz, tonic);
        targetHz_ = t.hz;
        degree_ = t.degree;
        offCents_ = t.cents;

        corr_.setAmount(clampf(p_[kAmount], 0.f, kMaxAmount));
        corr_.setGlide(std::max(0.f, p_[kRetune]));
        corr_.setDeadzone(std::max(0.f, p_[kDeadzone]));

        const double ratio = corr_.ratioFor(p.hz, t.hz);
        shift_.setPeriod(sr_ / p.hz);
        shift_.setRatio(ratio);
        moveCents_ = corr_.currentCents();
    }

    void publish() {
        const int cur = idx_.load(std::memory_order_relaxed);
        EngineView& v = views_[1 - cur];
        v.inHz = inHz_;
        v.targetHz = targetHz_;
        v.offCents = offCents_;
        v.moveCents = moveCents_;
        v.degree = degree_;
        v.confidence = conf_;
        v.voiced = voiced_;
        v.tab = tuning_.tab();
        idx_.store(1 - cur, std::memory_order_release);
    }

    void setDefaults() {
        p_[kTab] = 3.f;              // Sika
        p_[kTonic] = 60.f;           // C
        p_[kTonicFine] = 0.f;
        p_[kAmount] = 1.f;
        p_[kRetune] = 0.f;           // the hard snap, which is what this is for
        p_[kDeadzone] = 0.f;
        p_[kMix] = 1.f;
        p_[kOutput] = 0.f;
        p_[kBypass] = 0.f;
        p_[kSensitivity] = 0.5f;
        p_[kLive] = 0.f;             // studio: the full range, and the delay it costs
        tuning_.setTab(3);
    }

    double sr_ = 44100.0;
    float p_[kNumParams] = {0};

    tubu::Tuning tuning_;
    Detector det_;
    Corrector corr_;
    Shifter shift_, shiftR_;

    // work_ is preallocated: analyse() runs on the audio thread and used to
    // build its window with a fresh std::vector on every hop.
    std::vector<float> ring_, dry_, dryR_, mono_, work_;
    int wpos_ = 0, dpos_ = 0, dposR_ = 0, sinceHop_ = 0;

    double inHz_ = 0, targetHz_ = 0, offCents_ = 0, moveCents_ = 0, conf_ = 0;
    int degree_ = -1;
    bool voiced_ = false;

    EngineView views_[2];
    std::atomic<int> idx_{0};
};

} // namespace mtune
