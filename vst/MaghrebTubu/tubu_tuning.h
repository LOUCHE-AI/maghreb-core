// Maghreb Tubu - the tuning engine.
//
// Eighteen ṭubūʿ from Tunisian maʾlūf and Algerian ṣanʿa, as decoded from the
// Schwung module's dsp.so. Every label in the binary matched its manifest
// name, so these are the tunings the Move plays.
//
// The point of the whole thing is the neutral intervals - the 150, 350, 650
// and 850 cent degrees that 12-TET simply has no key for. The Move module and
// the Max for Live port both place them with pitch bend, which is per-channel
// in MIDI and therefore falls apart on chords. Here the tuning resolves to a
// frequency instead, so every voice can be placed exactly and independently.
//
// Framework-free, so the mapping can be checked in cents offline.

#pragma once
#include <cmath>
#include <algorithm>

namespace tubu {

static const int kNumTubu = 18;
static const int kDegrees = 7;

static const char* const kNames[kNumTubu] = {
    "Dhil", "Rasd", "Rasd al-Dhil", "Sika", "Iraq", "Hsin", "Hsin Ashiran",
    "Ramal", "Ramal Maya", "Maya", "Nawa", "Asbaayn", "Isbahan", "Zidan",
    "Mazmum", "Ghrib", "Mujannab", "Sika Mahjuba"
};

// Cents above the tonic. A degree that is not a multiple of 100 is one 12-TET
// cannot play.
static const int kFactory[kNumTubu][kDegrees] = {
    {0, 200, 400, 500, 700, 900, 1100},   // Dhil
    {0, 200, 400, 500, 700, 800, 1000},   // Rasd
    {0, 200, 400, 500, 700, 900, 1000},   // Rasd al-Dhil
    {0, 150, 350, 500, 700, 850, 1050},   // Sika
    {0, 150, 350, 500, 700, 850, 1000},   // Iraq
    {0, 150, 300, 500, 700, 800, 1000},   // Hsin
    {0, 200, 300, 500, 700, 800, 1000},   // Hsin Ashiran
    {0, 200, 350, 500, 700, 850, 1050},   // Ramal
    {0, 200, 350, 500, 700, 900, 1050},   // Ramal Maya
    {0, 200, 400, 500, 700, 900, 1000},   // Maya
    {0, 150, 300, 500, 700, 800, 1000},   // Nawa
    {0, 150, 300, 500, 700, 800, 1100},   // Asbaayn
    {0, 200, 300, 500, 700, 900, 1000},   // Isbahan
    {0, 200, 400, 500, 700, 900, 1100},   // Zidan
    // As decoded. The fourth and fifth are both 700 and the sixth and seventh
    // are both 900, so this tabʿ has five distinct degrees, not seven. That is
    // what the binary holds; it is almost certainly a defect in the original
    // table rather than a feature of the mode, but correcting it here would
    // mean this plays something the Move does not.
    {0, 200, 400, 700, 700, 900, 900},    // Mazmum
    {0, 150, 350, 500, 700, 850, 1050},   // Ghrib
    {0, 150, 300, 500, 650, 800, 1000},   // Mujannab
    {0, 150, 350, 500, 650, 850, 1050},   // Sika Mahjuba
};

enum MapMode {
    mapNearest = 0,   // pull an incoming pitch to the closest degree
    mapPerKey,        // the module's own: pitch class modulo seven
    mapPerStep,       // every key is the next degree, so the keyboard rises
    kNumMapModes
};

class Tuning {
public:
    Tuning() { setTab(3); }               // Sika, the module's default

    void setTab(int t) {
        tab_ = std::max(0, std::min(kNumTubu - 1, t));
        revert();
    }
    int  tab() const { return tab_; }
    const char* name() const { return kNames[tab_]; }

    void setTonic(int midi) { tonic_ = std::max(0, std::min(127, midi)); }
    int  tonic() const { return tonic_; }

    void setMode(int m) { mode_ = std::max(0, std::min(kNumMapModes - 1, m)); }
    int  mode() const { return mode_; }

    // Degrees are editable, because the written tables are one reading of a
    // tradition that is not fixed to the cent.
    void setDegreeCents(int degree, int cents) {
        if (degree < 0 || degree >= kDegrees) return;
        cents_[degree] = std::max(0, std::min(1200, cents));
    }
    int degreeCents(int degree) const {
        return (degree >= 0 && degree < kDegrees) ? cents_[degree] : 0;
    }
    void revert() {
        for (int i = 0; i < kDegrees; i++) cents_[i] = kFactory[tab_][i];
    }
    bool edited() const {
        for (int i = 0; i < kDegrees; i++)
            if (cents_[i] != kFactory[tab_][i]) return true;
        return false;
    }

    // How many degrees 12-TET cannot reach. This is the number that says
    // whether a tabʿ is exotic or just a mode you already know.
    int neutralCount() const {
        int n = 0;
        for (int i = 0; i < kDegrees; i++) if (cents_[i] % 100 != 0) n++;
        return n;
    }

    // Cents above the tonic for an incoming key.
    double centsFor(int pitch) const {
        const int rel = pitch - tonic_;

        if (mode_ == mapPerStep) {
            // every key steps one degree, so the keyboard is monotonic
            const int oct = (int)std::floor((double)rel / kDegrees);
            const int idx = rel - oct * kDegrees;
            return cents_[idx] + oct * 1200.0;
        }

        const int oct = (int)std::floor(rel / 12.0);
        const int pc  = rel - oct * 12;

        if (mode_ == mapPerKey)
            return cents_[pc % kDegrees] + oct * 1200.0;

        // nearest: the octave above the last degree is a candidate too
        const double want = pc * 100.0;
        int best = 0;
        double bestErr = 1e9;
        for (int i = 0; i < kDegrees; i++) {
            const double e = std::fabs(cents_[i] - want);
            if (e < bestErr) { bestErr = e; best = i; }
        }
        if (std::fabs(1200.0 - want) < bestErr) return 1200.0 + oct * 1200.0;
        return cents_[best] + oct * 1200.0;
    }

    // Where the key lands, as a fractional MIDI note.
    double resolvedMidi(int pitch) const {
        return tonic_ + centsFor(pitch) / 100.0;
    }

    // Whether a key resolves to something worth sounding. Per-step mapping
    // puts an octave every seven keys, so the top of a 128-key range reaches
    // eighteen octaves above the tonic - hundreds of kilohertz. The module
    // drops notes that fall outside MIDI range and so does this; without the
    // check the voice is asked for a frequency above the sample rate.
    bool playable(int pitch) const {
        const double m = resolvedMidi(pitch);
        return m >= 0.0 && m <= 127.0;
    }

    // The tuning resolved to a frequency. No pitch bend, no bend-range to
    // match, and every voice independent - which is what lets a chord hold
    // two neutral intervals at once.
    double frequencyFor(int pitch) const {
        return 440.0 * std::pow(2.0, (resolvedMidi(pitch) - 69.0) / 12.0);
    }

    // The tonic and its fifth, for the drone. Falls back to a plain 700 cents
    // if the tabʿ has no fifth degree.
    double droneFrequency(bool fifth) const {
        double cents = 0.0;
        if (fifth) cents = (kDegrees > 4) ? cents_[4] : 700.0;
        const double semis = tonic_ + cents / 100.0;
        return 440.0 * std::pow(2.0, (semis - 69.0) / 12.0);
    }

private:
    int tab_ = 3;
    int tonic_ = 60;
    int mode_ = mapPerKey;
    int cents_[kDegrees] = {0};
};

} // namespace tubu
