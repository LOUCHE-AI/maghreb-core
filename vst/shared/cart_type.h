// What kind of sound a DX7 voice is, read from the voice rather than its name.
//
// The names in a real collection are no help: of the thousand voices on this
// machine the ones that say what they are ("PIANO 3", "OB8 BRASS") are the
// minority, and the rest are "LAURIE", "SAGA", "PHAROH", "SloSwl //\". So the
// type is taken from the envelopes, which every voice has and none of them lie
// about: how long it takes to arrive, and whether it stays while a key is held.
//
// Four kinds, because a filter with more is a filter nobody uses:
//
//   Percussive  arrives at once and is gone       - bells, blocks, plucks
//   Plucked     arrives at once, rings down       - guitars, mallets, e-pianos
//   Sustained   arrives quickly, holds            - organs, brass, strings
//   Pad         takes its time to arrive          - washes, swells
//
// The unpacked DX7 voice is six operators of 21 bytes and then the globals.
// Per operator: rates at 0-3, levels at 4-7, output level at 16.

#pragma once

#include "dx_rates.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>

namespace mdx {

// Both halves of the answer in one list. The first nine come from the name
// when it says something - measured, a third of a real collection does - and
// the last four from the envelope, which is what the other two thirds get.
enum class VoiceType : uint8_t {
    Bass, Keys, Guitar, Bell, Organ, Brass, Strings, Lead,
    Pad, Sustained, Pluck, Perc,
    kCount
};

inline const char* voiceTypeName(VoiceType t) {
    switch (t) {
        case VoiceType::Bass:      return "Bass";
        case VoiceType::Keys:      return "Keys";
        case VoiceType::Guitar:    return "Guitar";
        case VoiceType::Bell:      return "Bell";
        case VoiceType::Organ:     return "Organ";
        case VoiceType::Brass:     return "Brass";
        case VoiceType::Strings:   return "Strings";
        case VoiceType::Lead:      return "Lead";
        case VoiceType::Pad:       return "Pad";
        case VoiceType::Sustained: return "Sustained";
        case VoiceType::Pluck:     return "Pluck";
        case VoiceType::Perc:      return "Perc";
        default:                   return "?";
    }
}

inline float rateSeconds(int rate) {
    if (rate < 0) rate = 0;
    if (rate > 99) rate = 99;
    return kEnvelopeRateSeconds[rate];
}

// What the name says, if it says anything. Checked before the envelope,
// because "BASS" is a better answer than "sustained" even when both are true.
inline bool typeFromName(const char* name, VoiceType& out) {
    if (!name) return false;
    std::string low;
    for (const char* c = name; *c; ++c)
        low.push_back((char)std::tolower((unsigned char)*c));

    struct Rule { VoiceType type; const char* words[8]; };
    static const Rule kRules[] = {
        {VoiceType::Bass,    {"bass", "bas ", "b.bs", "bs.", nullptr}},
        {VoiceType::Keys,    {"piano", "rhode", "rhodo", "e.p", "clav", "harpsi", "key", nullptr}},
        {VoiceType::Bell,    {"bell", "chime", "glass", "tubul", "celest", "vibe", "marimb", "xylo"}},
        {VoiceType::Organ,   {"organ", "hammon", "pipe", "church", nullptr}},
        {VoiceType::Guitar,  {"guit", "gtr", "banjo", "sitar", "koto", "harp", nullptr}},
        {VoiceType::Brass,   {"brass", "trump", "horn", "tuba", "tromb", "sax", "flute", "oboe"}},
        {VoiceType::Strings, {"string", "viol", "cello", "orch", "ensemb", nullptr}},
        {VoiceType::Lead,    {"lead", "solo", nullptr}},
        {VoiceType::Perc,    {"drum", "snare", "tom", "clap", "perc", "block", "kick", "cymb"}},
        {VoiceType::Pad,     {"pad", "sweep", "wash", "atmos", "space", nullptr}},
    };
    for (const Rule& r : kRules)
        for (int i = 0; i < 8 && r.words[i]; i++)
            if (low.find(r.words[i]) != std::string::npos) { out = r.type; return true; }
    return false;
}

inline VoiceType classifyVoice(const uint8_t patch[156]) {
    // Only operators loud enough to be heard have a say, and the louder one
    // has more of it - a quiet modulator's envelope is not the sound's shape.
    double attack = 0, sustain = 0, weight = 0;
    for (int op = 0; op < 6; op++) {
        const uint8_t* o = patch + op * 21;
        const double level = o[16];              // output level, 0..99
        if (level < 40) continue;                // too quiet to shape anything
        const double w = level / 99.0;
        attack += w * rateSeconds(o[0]);   // R1: time to L1
        sustain += w * (o[6] / 99.0);                    // L3: what it holds at
        weight += w;
    }
    if (weight <= 0) return VoiceType::Sustained;        // nothing loud: treat as held
    attack /= weight;
    sustain /= weight;

    if (attack > 0.25) return VoiceType::Pad;            // audibly slow to arrive
    if (sustain > 0.25) return VoiceType::Sustained;     // holds while held
    if (attack > 0.02) return VoiceType::Pluck;          // a moment to speak, then decays
    return VoiceType::Perc;
}

// The one to show: the name if it names something, the envelope otherwise.
inline VoiceType classify(const char* name, const uint8_t patch[156]) {
    VoiceType named;
    if (typeFromName(name, named)) return named;
    return classifyVoice(patch);
}

} // namespace mdx
