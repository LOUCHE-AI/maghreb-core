// Sets: a whole Maghreb Seq on disk.
//
// Plain text, keyed by parameter name rather than index, for the reason
// TapeGrain's sets are: an index-keyed blob silently mis-assigns every value
// the next time a parameter is inserted, and this plug-in's list has already
// grown twice this week - kEngine, kTone and kVelFM were appended after the
// section block precisely to avoid renumbering. Names make that unnecessary.
// A name the reader does not recognise is skipped; a parameter the file never
// mentions keeps whatever it was.
//
// A set also carries what the sequencer is playing *with*: the kit, the
// cartridge, and any per-lane cartridge voice. Those are stored as paths
// rather than as copies. The files are shared with Maghreb FM and Maghreb DX,
// so a set that embedded its own copy would quietly stop matching the kit the
// moment it was edited over there. The cost is that moving a .mkit leaves
// older sets falling back to the factory strokes, which is the right failure:
// visible, and fixed by pointing at the file again.
//
// Framework-free, so the round trip can be tested offline.

#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

namespace mseq {

static const char* const kSetMagic = "Maghreb Seq set 1";

// A lane either derives its patch from the stroke table, or plays a voice out
// of a cartridge. Each lane carries its own cartridge, so the reference is a
// path and a voice number per lane rather than one cartridge for the set.
struct LaneVoiceRef {
    std::string cart;
    int voice = -1;           // -1 means the lane is on its derived patch
};

struct SetRefs {
    std::string kitPath;      // .mkit, or empty for the factory strokes
    std::vector<LaneVoiceRef> lane;
};

// values are in engine units, one per name.
inline bool saveSet(const char* path,
                    const char* const* names, const float* values, int count,
                    const SetRefs& refs) {
    if (!path || !*path || !names || !values) return false;
    FILE* f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "%s\n", kSetMagic);
    for (int i = 0; i < count; i++) {
        if (!names[i] || !*names[i]) continue;
        // a name carrying the separator would produce a line that cannot be
        // read back, so it is dropped rather than written badly
        if (strchr(names[i], '|')) continue;
        fprintf(f, "P|%s|%.6f\n", names[i], values[i]);
    }
    if (!refs.kitPath.empty()) fprintf(f, "K|%s\n", refs.kitPath.c_str());
    for (size_t i = 0; i < refs.lane.size(); i++) {
        const LaneVoiceRef& r = refs.lane[i];
        if (r.voice < 0 || r.cart.empty()) continue;
        // path last, because it is the only field that may contain anything
        fprintf(f, "V|%d|%d|%s\n", (int)i, r.voice, r.cart.c_str());
    }

    const bool ok = (ferror(f) == 0);
    fclose(f);
    return ok;
}

// onParam once per recognised parameter line; refs filled from the rest.
inline bool loadSet(const char* path,
                    const std::function<void(const std::string&, double)>& onParam,
                    SetRefs& refs) {
    if (!path || !*path) return false;
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[4096];
    if (!fgets(line, sizeof line, f)) { fclose(f); return false; }
    if (strncmp(line, "Maghreb Seq set", 15) != 0) { fclose(f); return false; }

    refs = SetRefs();

    while (fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.size() < 3 || s[1] != '|') continue;

        if (s[0] == 'P') {
            const size_t bar = s.find('|', 2);
            if (bar == std::string::npos || bar == 2) continue;
            if (onParam) onParam(s.substr(2, bar - 2), atof(s.c_str() + bar + 1));
            continue;
        }
        if (s[0] == 'K') { refs.kitPath = s.substr(2); continue; }
        if (s[0] == 'V') {
            // V|lane|voice|path - the path is taken whole, separators and all
            const size_t b1 = s.find('|', 2);
            if (b1 == std::string::npos || b1 == 2) continue;
            const size_t b2 = s.find('|', b1 + 1);
            if (b2 == std::string::npos) continue;
            const int lane = atoi(s.c_str() + 2);
            const int voice = atoi(s.c_str() + b1 + 1);
            if (lane < 0 || lane > 63 || voice < 0) continue;
            if ((int)refs.lane.size() <= lane) refs.lane.resize(lane + 1);
            refs.lane[lane].voice = voice;
            refs.lane[lane].cart = s.substr(b2 + 1);
            continue;
        }
    }
    fclose(f);
    return true;
}

} // namespace mseq
