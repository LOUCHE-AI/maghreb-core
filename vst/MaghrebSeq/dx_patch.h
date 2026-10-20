// Turning a Maghreb FM stroke into a DX7 voice.
//
// The Move module's engine is two operators plus a band-passed noise layer,
// twelve numbers per stroke. msfa is a six-operator DX7. There is no
// conversion between them, so this is a derivation: each stroke is rebuilt as
// a DX7 patch that means the same thing.
//
//   body    OP1 carrier  <- OP2 modulator     ratio mode, so the pitch
//                                             envelope bends them
//   noise   OP5 carrier  <- OP6 modulator     fixed mode with feedback, so
//                                             it neither tracks the key nor
//                                             bends - as in the original,
//                                             where the noise band is set
//                                             once and does not follow hz
//   unused  OP3, OP4                          silenced
//
// on DX7 algorithm 5, which is three independent two-operator stacks with
// feedback available on operator 6.
//
// The three mappings this needs - envelope rate to seconds, pitch envelope
// level to semitones, and fixed-mode coarse/fine to hertz - were measured off
// the engine by calibrate.cpp rather than derived from the fixed-point tables
// inside msfa. Re-run it if msfa is ever updated.

#pragma once
#include "seq_data.h"

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace mdx {

using mseq::StrokeDef;
using mseq::kNumStrokes;

static const int kFirstNote = 36;      // as everywhere else in the family

// Seconds for a 60 dB decay at each DX7 envelope rate, measured.
static const float kRateSeconds[100] = {
    58.04989f, 58.04989f, 58.04989f, 58.04989f, 58.04989f,
    58.04989f, 58.04989f, 58.04989f, 58.04989f, 58.04989f,
    58.04989f, 52.96544f, 52.96544f, 46.56036f, 46.56036f,
    37.22522f, 30.91011f, 30.91011f, 26.65506f, 23.31501f,
    11.60998f, 11.60998f, 11.60998f, 11.60998f, 11.60998f,
    11.59982f, 11.59982f,  9.40481f,  9.40481f,  7.84980f,
     6.70476f,  6.70476f,  5.83474f,  4.64472f,  4.64472f,
     3.91474f,  3.36472f,  3.36472f,  2.94966f,  2.94966f,
     2.35465f,  1.94467f,  1.94467f,  1.66966f,  1.48463f,
     1.48463f,  1.20961f,  0.98467f,  0.98467f,  0.84463f,
     0.75465f,  0.75465f,  0.61460f,  0.61460f,  0.51955f,
     0.43465f,  0.43465f,  0.38458f,  0.29968f,  0.29968f,
     0.24961f,  0.23438f,  0.23438f,  0.19955f,  0.15456f,
     0.15456f,  0.13932f,  0.13932f,  0.10957f,  0.10449f,
     0.10449f,  0.07982f,  0.06458f,  0.06458f,  0.05950f,
     0.05442f,  0.05442f,  0.04426f,  0.04426f,  0.03483f,
     0.02975f,  0.02975f,  0.02467f,  0.02395f,  0.02395f,
     0.01959f,  0.01887f,  0.01887f,  0.01451f,  0.01379f,
     0.01379f,  0.01016f,  0.01016f,  0.00943f,  0.00871f,
     0.00871f,  0.00798f,  0.00726f,  0.00726f,  0.00508f,
};

// Pitch envelope level to semitones from centre, measured at these levels.
static const int   kPitchEgLevels[9] = {0, 12, 25, 37, 50, 62, 75, 87, 99};
static const float kPitchEgSemis[9]  = {-48.230f, -16.210f, -9.386f, -4.904f,
                                          0.000f,   4.536f,  9.414f, 15.035f,
                                         47.683f};

// The table runs from slow to fast, so the search walks down until it finds
// the first rate that is quick enough.
inline int rateForSeconds(double sec) {
    if (sec >= kRateSeconds[0]) return 0;
    for (int r = 99; r >= 0; r--)
        if (kRateSeconds[r] >= sec) return r;
    return 99;
}

inline int pitchEgLevelForSemis(double semis) {
    if (semis <= kPitchEgSemis[0]) return kPitchEgLevels[0];
    for (int i = 1; i < 9; i++) {
        if (semis <= kPitchEgSemis[i]) {
            const double t = (semis - kPitchEgSemis[i - 1]) /
                             (kPitchEgSemis[i] - kPitchEgSemis[i - 1]);
            return (int)std::lround(kPitchEgLevels[i - 1] +
                                    t * (kPitchEgLevels[i] - kPitchEgLevels[i - 1]));
        }
    }
    return kPitchEgLevels[8];
}

// A fixed-mode operator sounds at 10 ^ (V / 100), where V is coarse*100+fine.
// Confirmed against the engine to within a third of a percent.
inline void fixedFreqBytes(double hz, int& coarse, int& fine) {
    int V = (int)std::lround(100.0 * std::log10(std::max(1.0, hz)));
    V = std::max(0, std::min(399, V));
    coarse = V / 100;
    fine = V % 100;
}

// A ratio-mode operator multiplies the note. Coarse 0 means one half.
inline void ratioBytes(double ratio, int& coarse, int& fine) {
    ratio = std::max(0.5, std::min(31.99, ratio));
    if (ratio < 1.0) { coarse = 0; fine = (int)std::lround((ratio / 0.5 - 1.0) * 100.0); }
    else {
        coarse = (int)ratio;
        fine = (int)std::lround((ratio / coarse - 1.0) * 100.0);
        if (fine > 99) { fine = 0; coarse = std::min(31, coarse + 1); }
    }
    coarse = std::max(0, std::min(31, coarse));
    fine = std::max(0, std::min(99, fine));
}

// DX7 output level is roughly six dB per eight counts near the top, so an
// amplitude maps back as 99 + 8 * log2(amp). Anything quieter than about
// -75 dB is simply off.
inline int levelForAmp(double amp) {
    if (amp <= 0.0001) return 0;
    const int l = (int)std::lround(99.0 + 8.0 * std::log2(std::min(1.0, amp)));
    return std::max(0, std::min(99, l));
}

// One operator's 21 bytes. Operators are stored backwards: op1 sits last.
inline void setOp(uint8_t* p, int op, int r1, int r2, int r3, int r4,
                  int l1, int l2, int l3, int l4, int outLevel,
                  int mode, int coarse, int fine, int detune) {
    uint8_t* o = p + (5 - op) * 21;
    o[0] = (uint8_t)r1; o[1] = (uint8_t)r2; o[2] = (uint8_t)r3; o[3] = (uint8_t)r4;
    o[4] = (uint8_t)l1; o[5] = (uint8_t)l2; o[6] = (uint8_t)l3; o[7] = (uint8_t)l4;
    o[8] = 0; o[9] = 0; o[10] = 0; o[11] = 0; o[12] = 0;   // keyboard scaling
    o[13] = 0;                                             // rate scaling
    o[14] = 0;                                             // amp mod sens
    o[15] = 0;                                             // key vel sens
    o[16] = (uint8_t)outLevel;
    o[17] = (uint8_t)mode;                                 // 0 ratio, 1 fixed
    o[18] = (uint8_t)coarse;
    o[19] = (uint8_t)fine;
    o[20] = (uint8_t)(detune + 7);
}

// Everything a voice needs beyond the stroke itself.
struct DeriveSettings {
    float kitFreq = 1.f, kitDecay = 1.f, kitNoise = 1.f, kitIndex = 1.f;
    float tune = 0.f;          // semitones
    float decay = 1.f;         // scales every envelope
    float fmAmount = 1.f;
    float noiseAmount = 1.f;
    float tone = 0.5f;         // tilts the noise band
    float velocity = 1.f;
    float velFM = 0.55f;
};

// Builds the patch for one stroke, played on its own note.
inline void derivePatch(const StrokeDef& d, int strokeIndex,
                        const DeriveSettings& s, uint8_t out[156]) {
    for (int i = 0; i < 156; i++) out[i] = 0;

    const int note = kFirstNote + strokeIndex;
    const double noteHz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    const double tuneMul = std::pow(2.0, s.tune / 12.0);
    const double dscale = std::max(0.05f, s.decay) * s.kitDecay;

    const double bodyHz = d.freq * s.kitFreq * tuneMul;
    const double velFm = 1.0 - s.velFM * (1.0 - s.velocity);
    const double index = d.index * s.fmAmount * s.kitIndex * velFm;

    // ---- body: OP1 carrier, OP2 modulator, both ratio mode ----
    int c, f;
    ratioBytes(bodyHz / noteHz, c, f);
    setOp(out, 0, 99, rateForSeconds(d.ampDecay * dscale), 0, 0,
          99, 0, 0, 0, levelForAmp(d.gain * s.velocity), 0, c, f, 0);

    ratioBytes(bodyHz * d.ratio / noteHz, c, f);
    setOp(out, 1, 99, rateForSeconds(d.fmDecay * dscale), 0, 0,
          99, 0, 0, 0, levelForAmp(index / 4.0), 0, c, f, 0);

    // ---- silence the pair that is not used ----
    setOp(out, 2, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 1, 0, 0);
    setOp(out, 3, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 1, 0, 0);

    // ---- noise: OP5 carrier, OP6 modulator, both fixed ----
    // Fixed mode is the point: the original sets its noise band once from
    // noiseFreq and never bends it, and a fixed operator ignores the pitch
    // envelope for free.
    const double tilt = 0.45 + s.tone * 1.35;
    const double nf = std::min(d.noiseFreq * tilt * tuneMul, 9000.0);
    const double noise = d.noise * s.noiseAmount * s.kitNoise;

    fixedFreqBytes(nf, c, f);
    setOp(out, 4, 99, rateForSeconds(d.noiseDecay * dscale), 0, 0,
          99, 0, 0, 0, levelForAmp(noise * 0.9 * s.velocity), 1, c, f, 0);

    // A modulator an octave and a bit above, driven hard and fed back on
    // itself, is how a DX7 makes noise - there is no noise source in it.
    // Feedback lives on operator 6, which is why the noise pair is this one.
    fixedFreqBytes(nf * 2.37, c, f);
    setOp(out, 5, 99, rateForSeconds(d.noiseDecay * dscale * 0.8), 0, 0,
          99, 0, 0, 0, noise > 0.0001 ? 92 : 0, 1, c, f, 3);

    // ---- pitch envelope: the drop that makes it a drum ----
    // The envelope starts at L4 and moves toward L1, so the strike level goes
    // in L4 and the rest position in L1.
    const int lift = pitchEgLevelForSemis(12.0 * std::log2(1.0 + d.bend));
    out[126] = (uint8_t)rateForSeconds(d.bendDecay * dscale);   // R1
    out[127] = 99; out[128] = 99; out[129] = 99;
    out[130] = 50; out[131] = 50; out[132] = 50;                // L1..L3 rest
    out[133] = (uint8_t)lift;                                   // L4 is the start

    out[134] = 4;    // algorithm 5: three two-operator stacks
    out[135] = 7;    // feedback, on operator 6 - the noise
    out[136] = 1;    // oscillator key sync, so every strike starts alike
    out[137] = 35;   // lfo speed, unused
    out[138] = 0; out[139] = 0; out[140] = 0; out[141] = 0; out[142] = 0;
    out[143] = 0;    // pitch mod sensitivity
    out[144] = 24;   // no transpose
    for (int i = 145; i < 156; i++) out[i] = ' ';
}

} // namespace mdx
