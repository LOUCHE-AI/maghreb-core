// Preset files: your own sets, on disk.
//
// Plain text, keyed by parameter name rather than index. An index-keyed blob
// would silently mis-assign every value the next time a parameter is
// inserted, and this plug-in's parameter list has already grown twice. With
// names, an old set still loads after the list changes: names it does not
// recognise are skipped, and parameters the file never mentioned keep
// whatever they are set to.
//
// Framework-free on purpose, so the round trip can be tested offline.

#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <functional>

namespace tg {

static const char* const kPresetMagic = "TapeGrain set 1";

// values are in engine units, one per name. samplePaths is one per stem slot;
// empty entries are simply not written.
inline bool savePreset(const char* path,
                       const char* const* names, const float* values, int count,
                       const std::string* samplePaths, int numSlots) {
    if (!path || !*path || !names || !values) return false;
    FILE* f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "%s\n", kPresetMagic);
    for (int i = 0; i < count; i++) {
        if (!names[i]) continue;
        fprintf(f, "P|%s|%.6f\n", names[i], values[i]);
    }
    for (int i = 0; samplePaths && i < numSlots; i++)
        if (!samplePaths[i].empty())
            fprintf(f, "S|%d|%s\n", i, samplePaths[i].c_str());
    fclose(f);
    return true;
}

// onParam is called once per recognised line; onSample once per stem slot the
// file carries.
inline bool loadPreset(const char* path,
                       const std::function<void(const std::string&, double)>& onParam,
                       const std::function<void(int, const std::string&)>& onSample) {
    if (!path || !*path) return false;
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[2048];
    if (!fgets(line, sizeof line, f)) { fclose(f); return false; }
    if (strncmp(line, "TapeGrain set", 13) != 0) { fclose(f); return false; }

    std::vector<std::pair<int, std::string>> samples;
    while (fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.size() < 3 || s[1] != '|') continue;

        if (s[0] == 'S') {
            // "S|2|path" carries a slot; the older "S|path" did not, and
            // means the first one
            if (s.size() > 4 && s[2] >= '0' && s[2] <= '9' && s[3] == '|')
                samples.emplace_back(s[2] - '0', s.substr(4));
            else
                samples.emplace_back(0, s.substr(2));
            continue;
        }
        if (s[0] != 'P') continue;

        const size_t bar = s.find('|', 2);
        if (bar == std::string::npos || bar == 2) continue;
        if (onParam) onParam(s.substr(2, bar - 2), atof(s.c_str() + bar + 1));
    }
    fclose(f);

    // last, so the parameters are already in place when the audio arrives
    if (onSample)
        for (size_t i = 0; i < samples.size(); i++)
            onSample(samples[i].first, samples[i].second);
    return true;
}

} // namespace tg
