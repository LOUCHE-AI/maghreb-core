// Kit files, checked the same way the TapeGrain sets are: the failure that
// matters is a file written by one build loading wrongly into another.

#include "kit_preset.h"
#include <cstdio>
#include <cmath>
#include <map>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static const char* kNames[] = {"Kit", "Tune", "Decay", "Humanize", "Output"};
static const int kN = 5;
static const int kStrokes = 11;

int main() {
    char msg[300];
    const char* path = "test_kit.mkit";

    float vals[kN] = {2.f, -3.5f, 1.75f, 0.42f, -6.f};
    std::vector<float> table(kStrokes * kit::kStrokeFields);
    for (int s = 0; s < kStrokes; s++)
        for (int k = 0; k < kit::kStrokeFields; k++)
            table[s * kit::kStrokeFields + k] = (float)(s * 100 + k) + 0.5f;

    ok("saves", kit::saveKit(path, kNames, vals, kN, table.data(), kStrokes));

    // ---- round trip ----
    {
        std::map<std::string, double> got;
        std::map<int, std::vector<float>> strokes;
        const bool r = kit::loadKit(path,
            [&](const std::string& n, double v) { got[n] = v; },
            [&](int i, const float* v) {
                strokes[i] = std::vector<float>(v, v + kit::kStrokeFields);
            });

        bool params = r && (int)got.size() == kN;
        for (int i = 0; i < kN && params; i++)
            if (std::fabs(got[kNames[i]] - vals[i]) > 1e-4) params = false;
        snprintf(msg, sizeof msg, "%d of %d parameters", (int)got.size(), kN);
        ok("every parameter comes back as written", params, msg);

        bool tbl = (int)strokes.size() == kStrokes;
        for (int s = 0; s < kStrokes && tbl; s++)
            for (int k = 0; k < kit::kStrokeFields; k++)
                if (std::fabs(strokes[s][k] - table[s * kit::kStrokeFields + k]) > 1e-3)
                    tbl = false;
        snprintf(msg, sizeof msg, "%d of %d strokes, %d fields each",
                 (int)strokes.size(), kStrokes, kit::kStrokeFields);
        ok("every stroke's twelve numbers and its pan come back", tbl, msg);
    }

    // ---- a kit from a build with a parameter this one lacks ----
    {
        FILE* f = fopen("test_future.mkit", "w");
        fprintf(f, "%s\nP|Tune|4.000000\nP|Reverb|0.900000\nP|Output|-2.000000\n",
                kit::kMagic);
        fclose(f);
        std::map<std::string, double> got;
        int unknown = 0;
        const bool r = kit::loadKit("test_future.mkit",
            [&](const std::string& n, double v) {
                bool known = false;
                for (int i = 0; i < kN; i++) if (n == kNames[i]) known = true;
                if (known) got[n] = v; else unknown++;
            },
            [](int, const float*) {});
        snprintf(msg, sizeof msg, "%d known, %d skipped", (int)got.size(), unknown);
        ok("an unrecognised parameter is skipped and the rest still load",
           r && got.size() == 2 && unknown == 1, msg);
    }

    // ---- a kit with no stroke table at all ----
    {
        FILE* f = fopen("test_noparams.mkit", "w");
        fprintf(f, "%s\nP|Tune|1.000000\n", kit::kMagic);
        fclose(f);
        int strokes = 0;
        const bool r = kit::loadKit("test_noparams.mkit",
            [](const std::string&, double) {},
            [&](int, const float*) { strokes++; });
        ok("a kit with no strokes loads its parameters and leaves the table alone",
           r && strokes == 0);
    }

    // ---- things that should fail cleanly ----
    {
        ok("a missing file fails cleanly",
           !kit::loadKit("no_such.mkit", [](const std::string&, double) {},
                         [](int, const float*) {}));

        FILE* f = fopen("test_junk.mkit", "w");
        fprintf(f, "not a kit\nP|Tune|1.0\n");
        fclose(f);
        int calls = 0;
        const bool junk = kit::loadKit("test_junk.mkit",
            [&](const std::string&, double) { calls++; },
            [](int, const float*) {});
        snprintf(msg, sizeof msg, "%d values read", calls);
        ok("a file that is not a kit is refused, not half-applied",
           !junk && calls == 0, msg);

        // a stroke row cut short must not be applied with garbage in the rest
        f = fopen("test_short.mkit", "w");
        fprintf(f, "%s\nS|3|1.0|2.0|3.0\nS|4", kit::kMagic);
        for (int k = 0; k < kit::kStrokeFields; k++) fprintf(f, "|%d.0", k);
        fprintf(f, "\n");
        fclose(f);
        std::vector<int> applied;
        kit::loadKit("test_short.mkit", [](const std::string&, double) {},
                     [&](int i, const float*) { applied.push_back(i); });
        snprintf(msg, sizeof msg, "%d rows applied", (int)applied.size());
        ok("a truncated stroke row is dropped and the complete one still loads",
           applied.size() == 1 && applied[0] == 4, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
