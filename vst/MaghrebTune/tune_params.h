// The parameter table, with nothing plug-in-format about it.
//
// It lived in the VST3 wrapper until there was a second wrapper. Two copies of
// a table like this drift the first time a range or a default is edited in one
// of them, and the symptom is a preset that means different things depending on
// which format loaded it. So there is one table, and both wrappers read it.

#pragma once

#include "tune_dsp.h"
#include "tubu_tuning.h"

#include <string>
#include <vector>

namespace mtune {

struct ParamDef {
    std::string name;
    double lo, hi, def;
    int steps;
    const char* const* names;   // enum value names, or null
    int numNames;
    std::string unit;
    double dispLo, dispHi;
    int precision;
    std::string help;
};

const std::vector<ParamDef>& params();

inline const char* const kOffOn[] = {"Off", "On"};

// The eighteen, as a stable array of pointers for StringListParameter.
inline const char* const* tabNames() {
    static std::vector<const char*> v;
    if (v.empty())
        for (int i = 0; i < tubu::kNumTubu; i++) v.push_back(tubu::kNames[i]);
    return v.data();
}

inline const std::vector<ParamDef>& params() {
    static std::vector<ParamDef> P;
    if (!P.empty()) return P;
    P.resize(kNumParams);

    auto set = [&](int id, const char* name, double lo, double hi, double def,
                   int steps, const char* const* names, int numNames,
                   const char* unit, double dLo, double dHi, int prec,
                   const char* help) {
        ParamDef& d = P[id];
        d.name = name; d.lo = lo; d.hi = hi; d.def = def; d.steps = steps;
        d.names = names; d.numNames = numNames; d.unit = unit;
        d.dispLo = dLo; d.dispHi = dHi; d.precision = prec; d.help = help;
    };

    set(kTab, "Tab", 0, tubu::kNumTubu - 1, 3, tubu::kNumTubu - 1,
        tabNames(), tubu::kNumTubu, "", 0, tubu::kNumTubu - 1, 0,
        "Which of the eighteen the voice is corrected to. The degrees that are not "
        "multiples of a hundred cents are the ones an ordinary corrector cannot reach.");
    set(kTonic, "Tonic", 36, 84, 60, 48, nullptr, 0, "", 36, 84, 0,
        "Where the tabʿ starts. Every degree is measured in cents above this note.");
    set(kTonicFine, "Tonic Fine", -50, 50, 0, 0, nullptr, 0, "c", -50, 50, 0,
        "Moves the whole grid, so a singer who is not at concert pitch is met "
        "rather than dragged across it.");
    set(kAmount, "Amount", 0, 1.5, 1, 0, nullptr, 0, "%", 0, 150, 0,
        "How far towards the degree. Zero leaves the voice alone; a hundred pins "
        "it. Past a hundred it overshoots - a note thirty cents flat is moved "
        "forty-five and comes out fifteen sharp of the degree. That is not more in "
        "tune, it is more obviously tuned, and it is where the sound stops being a "
        "correction and starts being the effect.");
    set(kRetune, "Retune", 0, 200, 0, 0, nullptr, 0, "ms", 0, 200, 0,
        "How fast it gets there. Zero is the hard snap raï is built on - you hear "
        "the note step onto the degree. Slower keeps the singer's approach and only "
        "fixes where the note lands.");
    set(kDeadzone, "Deadzone", 0, 50, 0, 0, nullptr, 0, "c", 0, 50, 0,
        "How far a note may lean before it is touched at all, so an expressive "
        "approach to a degree is not flattened the moment it begins.");
    set(kMix, "Mix", 0, 1, 1, 0, nullptr, 0, "%", 0, 100, 0,
        "Corrected against dry. The dry is delayed to meet the corrected signal, "
        "so a blend is a blend rather than a comb filter.");
    set(kOutput, "Output", -24, 12, 0, 0, nullptr, 0, "dB", -24, 12, 1,
        "Output trim.");
    set(kBypass, "Bypass", 0, 1, 0, 1, kOffOn, 2, "", 0, 1, 0,
        "Passes the input through untouched, sample for sample.");
    set(kSensitivity, "Sensitivity", 0, 1, 0.5, 0, nullptr, 0, "%", 0, 100, 0,
        "How readily a sound is called a note. Turn it up when the tracker keeps "
        "letting go mid-word on a breathy or distant vocal; turn it down if it "
        "starts finding notes in breaths and room noise.");
    set(kLive, "Live", 0, 1, 0, 1, kOffOn, 2, "", 0, 1, 0,
        "Halves the delay you hear while singing through it, by giving up the "
        "bottom of the range. Off, the grain is sized for a 70 Hz voice and the "
        "plug-in reports 31 ms; on, it stops following anything below 100 Hz - a "
        "low male lead at A2 still tracks - and reports 23. Off is right for a "
        "take being corrected afterwards, where the host lines the latency back up "
        "and none of it is heard. Switching mode drops what is in flight, so do it "
        "between takes rather than during one.");
    return P;
}


} // namespace mtune
