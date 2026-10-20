// Kits on disk.
//
// Two things have to be saved: the parameters, and the eleven strokes' own
// twelve numbers. The parameters are keyed by name for the same reason
// TapeGrain's sets are - the list has grown twice already, and an index-keyed
// file mis-assigns every value the next time one is inserted. The stroke
// table is keyed by stroke index, which cannot change: there are eleven
// strokes because the module has eleven.
//
// Framework-free, so the round trip can be tested offline. Shared verbatim by
// Maghreb FM and Maghreb DX, which have the same shape.

#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

namespace kit {

static const char* const kMagic = "Maghreb kit 1";
static const int kStrokeFields = 13;   // twelve numbers plus pan

// values are in engine units, one per name; table is numStrokes * 13
inline bool saveKit(const char* path,
                    const char* const* names, const float* values, int count,
                    const float* table, int numStrokes) {
    if (!path || !*path || !names || !values) return false;
    FILE* f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "%s\n", kMagic);
    for (int i = 0; i < count; i++) {
        if (!names[i]) continue;
        fprintf(f, "P|%s|%.6f\n", names[i], values[i]);
    }
    for (int s = 0; s < numStrokes && table; s++) {
        fprintf(f, "S|%d", s);
        for (int k = 0; k < kStrokeFields; k++)
            fprintf(f, "|%.6f", table[s * kStrokeFields + k]);
        fprintf(f, "\n");
    }
    fclose(f);
    return true;
}

// onParam once per recognised parameter line; onStroke once per stroke row,
// with kStrokeFields values.
inline bool loadKit(const char* path,
                    const std::function<void(const std::string&, double)>& onParam,
                    const std::function<void(int, const float*)>& onStroke) {
    if (!path || !*path) return false;
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[4096];
    if (!fgets(line, sizeof line, f)) { fclose(f); return false; }
    if (strncmp(line, "Maghreb kit", 11) != 0) { fclose(f); return false; }

    while (fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.size() < 3 || s[1] != '|') continue;

        if (s[0] == 'S') {
            // S|index|v0|v1|...
            const char* p = s.c_str() + 2;
            char* end = nullptr;
            const long idx = strtol(p, &end, 10);
            if (end == p || idx < 0) continue;
            float v[kStrokeFields];
            int got = 0;
            while (got < kStrokeFields && end && *end == '|') {
                const char* q = end + 1;
                v[got] = (float)strtod(q, &end);
                if (end == q) break;
                got++;
            }
            // a short row is a truncated file, not a stroke worth applying
            if (got == kStrokeFields && onStroke) onStroke((int)idx, v);
            continue;
        }
        if (s[0] != 'P') continue;

        const size_t bar = s.find('|', 2);
        if (bar == std::string::npos || bar == 2) continue;
        if (onParam) onParam(s.substr(2, bar - 2), atof(s.c_str() + bar + 1));
    }
    fclose(f);
    return true;
}

} // namespace kit
