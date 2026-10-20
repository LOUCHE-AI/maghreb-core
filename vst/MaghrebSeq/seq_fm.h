// Maghreb FM - the drum voice.
//
// A port of the Move module's engine: two-operator FM with a pitch bend on
// the carrier, plus a band-passed noise layer, exponential decays throughout,
// and a tanh saturator at the end. The stroke table and the kit multipliers
// are the module's own, in seq_data.h.
//
// This is deliberately the same arithmetic as the Python reference that
// rendered the 33 check files, so the sound can be compared against them
// rather than taken on trust.
//
// Framework-free, so it can be rendered and measured offline.

#pragma once
#include "seq_data.h"

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace mseq {

// Per-voice controls, on top of the stroke and kit tables.
struct DrumSettings {
    float tune = 0.f;        // semitones
    float decay = 1.f;       // multiplies every decay
    float fmAmount = 1.f;
    float noiseAmount = 1.f;
    float tone = 0.5f;       // tilts the noise band
    float drive = 0.12f;
    float level = 0.9f;
};

class DrumVoice {
public:
    void setSampleRate(double sr) { sr_ = (sr > 0) ? sr : 44100.0; }

    bool active() const { return active_; }

    // Cut, rather than released: for the moment before an offline render,
    // where anything still ringing from live playing would be printed into
    // the file as a hit nobody asked for.
    void silence() { active_ = false; }

    void trigger(int stroke, int kit, float vel, const DrumSettings& s) {
        if (stroke < 0 || stroke >= kNumStrokes) return;
        trigger(kStrokes[stroke], kit, vel, s);
    }

    // The same strike from a stroke handed in, so a kit loaded from disk is
    // played rather than the factory table.
    void trigger(const StrokeDef& d, int kit, float vel, const DrumSettings& s) {
        const KitDef& k = kKits[std::max(0, std::min(kNumKits - 1, kit))];

        const double tuneMul = std::pow(2.0, s.tune / 12.0);
        const double dscale = std::max(0.05f, s.decay) * k.decay;

        baseHz_ = d.freq * k.freq * tuneMul;
        ratio_ = d.ratio;
        index_ = d.index * s.fmAmount * k.index;
        bend_ = d.bend;
        gain_ = d.gain;
        vel_ = std::max(0.f, std::min(1.f, vel));

        noiseAmt_ = d.noise * s.noiseAmount * k.noise;
        const double tilt = 0.45 + s.tone * 1.35;
        const double nf = std::min(d.noiseFreq * tilt * tuneMul, sr_ * 0.42);

        da_ = decayCoef(d.ampDecay * dscale);
        dm_ = decayCoef(d.fmDecay  * dscale);
        dp_ = decayCoef(d.bendDecay * dscale);
        dn_ = decayCoef(d.noiseDecay * dscale);

        drive_ = 1.0 + s.drive * 4.0;
        makeup_ = 1.0 / (1.0 + s.drive * 1.2);
        level_ = s.level;

        // one-pole state-variable band-pass, as the module builds it
        fco_ = std::min(2.0 * std::sin(3.14159265358979 * nf / sr_), 0.9);
        q_ = 1.0 / std::max(d.noiseQ, 0.5f);

        envA_ = envM_ = envP_ = envN_ = 1.0;
        phC_ = phM_ = 0.0;
        bpLo_ = bpBand_ = 0.0;
        // a fixed seed per hit keeps a rendered pattern identical run to run,
        // which is what makes an offline comparison meaningful
        rng_ = 0x1234567;
        active_ = true;
    }

    // Adds this voice into L/R. Returns false once it has finished.
    bool addTo(float* L, float* R, int n, float pan) {
        if (!active_) return false;
        const double isr = 1.0 / sr_;
        const double gl = 1.0 - std::max(0.f, std::min(1.f, pan));
        const double gr = std::max(0.f, std::min(1.f, pan));

        for (int i = 0; i < n; i++) {
            const double hz = baseHz_ * (1.0 + bend_ * envP_);

            phM_ += hz * ratio_ * isr;
            if (phM_ >= 1.0) phM_ -= 1.0;
            const double mod = std::sin(6.283185307179586 * phM_) * index_ * envM_;

            phC_ += hz * isr;
            if (phC_ >= 1.0) phC_ -= 1.0;
            double ph = phC_ + mod;
            ph -= std::floor(ph);
            const double tone = std::sin(6.283185307179586 * ph) * envA_;

            double nz = 0.0;
            if (noiseAmt_ > 0.0001) {
                rng_ = rng_ * 1664525u + 1013904223u;
                const double w = ((double)(rng_ >> 8) - 8388608.0) / 8388608.0;
                const double hi = w - bpLo_ - q_ * bpBand_;
                bpBand_ += fco_ * hi;
                bpLo_ += fco_ * bpBand_;
                nz = bpBand_ * noiseAmt_ * envN_;
            }

            double mix = (tone + nz) * vel_ * gain_;
            envA_ *= da_; envM_ *= dm_; envP_ *= dp_; envN_ *= dn_;

            mix *= 0.42;
            mix = std::tanh(mix * drive_) * makeup_ * level_;

            L[i] += (float)(mix * gl);
            R[i] += (float)(mix * gr);

            if (envA_ < 0.0004 && envN_ < 0.0004) { active_ = false; return false; }
        }
        return true;
    }

private:
    // -6.9078 is ln(1/1000): the coefficient that takes an envelope down 60 dB
    // in the stated time. The floor stops a zero decay dividing by nothing.
    double decayCoef(double seconds) const {
        if (seconds < 0.0005) seconds = 0.0005;
        return std::exp(-6.9078 / (seconds * sr_));
    }

    double sr_ = 44100.0;
    bool active_ = false;

    double baseHz_ = 100, ratio_ = 1, index_ = 0, bend_ = 0;
    double gain_ = 1, vel_ = 1, noiseAmt_ = 0;
    double da_ = 0, dm_ = 0, dp_ = 0, dn_ = 0;
    double drive_ = 1, makeup_ = 1, level_ = 1;
    double fco_ = 0.1, q_ = 1;

    double envA_ = 0, envM_ = 0, envP_ = 0, envN_ = 0;
    double phC_ = 0, phM_ = 0, bpLo_ = 0, bpBand_ = 0;
    uint32_t rng_ = 0x1234567;
};

} // namespace mseq
