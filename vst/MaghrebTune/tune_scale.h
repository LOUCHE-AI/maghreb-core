// Where a sung note should go.
//
// This is the whole reason the plug-in exists. A twelve-tone corrector snaps
// to the nearest semitone, and the ṭubūʿ are full of degrees that are not on
// one - Sika's third sits at 350 cents, half way between E flat and E. Run a
// singer through an ordinary autotune and every one of those gets pulled to
// the nearest 100, which is precisely the interval that made it that ṭabʿ.
//
// So the target grid is tubu::Tuning's degrees, in cents above a tonic, and
// the nearest degree is searched across octaves rather than assumed.
//
// Framework-free: the snapping is arithmetic and is tested as arithmetic.

#pragma once
#include "tubu_tuning.h"

#include <cmath>
#include <algorithm>

namespace mtune {

struct Target {
    double hz = 0.0;        // where the note should sit
    double cents = 0.0;     // how far it has to move, target - detected
    int degree = -1;        // which degree of the ṭabʿ it landed on
    int octave = 0;         // relative to the tonic's octave
    bool found = false;
};

// Middle A is the only fixed point in any of this.
inline double hzForMidi(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}
inline double midiForHz(double hz) {
    return (hz > 0.0) ? 69.0 + 12.0 * std::log2(hz / 440.0) : 0.0;
}

// The nearest degree of the ṭabʿ to a detected frequency.
//
// `tonicMidi` may be fractional, so a singer who is a little flat of concert
// pitch can have the whole grid moved to meet them rather than being dragged
// across it.
inline Target nearestDegree(const tubu::Tuning& t, double hz, double tonicMidi) {
    Target out;
    if (hz <= 0.0) return out;

    const double tonicHz = hzForMidi(tonicMidi);
    const double cents = 1200.0 * std::log2(hz / tonicHz);

    // Reduce into one octave, keeping the octave number, then look at the
    // degrees of that octave and the first of the next: a note a few cents
    // below the tonic belongs to the octave below, and the nearest degree to
    // it is that octave's last, not this one's first.
    double best = 1e30;
    for (int oct = -1; oct <= 1; oct++) {
        const double base = std::floor(cents / 1200.0) * 1200.0 + oct * 1200.0;
        for (int d = 0; d < tubu::kDegrees; d++) {
            const double cand = base + t.degreeCents(d);
            const double dist = std::fabs(cents - cand);
            if (dist < best) {
                best = dist;
                out.cents = cand - cents;
                out.degree = d;
                out.octave = (int)std::llround((base) / 1200.0);
                out.hz = tonicHz * std::pow(2.0, cand / 1200.0);
                out.found = true;
            }
        }
    }
    return out;
}

// What an ordinary twelve-tone corrector would do with the same note, for the
// comparison the plug-in is built to win.
inline double nearestSemitoneHz(double hz) {
    if (hz <= 0.0) return 0.0;
    return hzForMidi(std::llround(midiForHz(hz)));
}

// The correction itself, with the two controls every corrector has.
//
// `amount` is how far towards the target to go, 0 leaving the voice alone and
// 1 pinning it. `glideMs` is how fast it gets there: instant is the hard
// sound, slower keeps the singer's own approach to the note and only fixes
// where it lands.
class Corrector {
public:
    void setup(double sampleRate, int hop) {
        sr_ = (sampleRate > 0) ? sampleRate : 44100.0;
        hop_ = std::max(1, hop);
        current_ = 0.0;
        have_ = false;
    }
    // Past 1 the correction overshoots: a note thirty cents flat is moved
    // forty-five and lands fifteen cents sharp of the degree. It is not more
    // in tune - it is more obviously tuned, which is the sound raï wants and
    // the reason this goes past a hundred at all.
    void setAmount(double a) { amount_ = std::max(0.0, std::min(1.5, a)); }
    void setGlide(double ms) { glideMs_ = std::max(0.0, ms); }
    // A note has to be at least this far off before it is touched at all, so
    // an expressive lean into a degree is not flattened the moment it starts.
    void setDeadzone(double cents) { dead_ = std::max(0.0, cents); }

    void reset() { current_ = 0.0; have_ = false; }

    // Returns the ratio to shift by: 1 is no change.
    double ratioFor(double detectedHz, double targetHz) {
        // With no note to work from, hold the correction where it is rather
        // than snapping back to none: a consonant in the middle of a word
        // should not undo the line.
        if (detectedHz <= 0.0 || targetHz <= 0.0)
            return std::pow(2.0, current_ / 1200.0);

        double wantCents = 1200.0 * std::log2(targetHz / detectedHz);
        if (std::fabs(wantCents) < dead_) wantCents = 0.0;
        wantCents *= amount_;

        // One pole towards the wanted correction, per hop. It starts from
        // wherever the correction currently is - zero on a fresh note - so
        // the retune speed is audible on the attack, which is the whole
        // difference between a hard snap and a correction you cannot hear.
        const double tau = glideMs_ * 0.001 * sr_;
        const double a = (tau > 1.0) ? std::exp(-(double)hop_ / tau) : 0.0;
        current_ = current_ * a + wantCents * (1.0 - a);
        have_ = true;
        return std::pow(2.0, current_ / 1200.0);
    }

    double currentCents() const { return current_; }

private:
    double sr_ = 44100.0;
    int hop_ = 256;
    double amount_ = 1.0, glideMs_ = 30.0, dead_ = 0.0;
    double current_ = 0.0;
    bool have_ = false;
};

} // namespace mtune
