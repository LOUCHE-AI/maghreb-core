// Preset files have to survive the thing that actually happens to this
// plug-in: the parameter list keeps growing. These check that a set written
// by one build still loads into a different one.

#include "tapegrain_preset.h"
#include "tapegrain_dsp.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const char* x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x[0] ? "  " : "", x);
    if (!c) fails++;
}

static const char* kNames[] = {
    "Position", "Grain Size", "Density", "Pitch", "Level", "Agents", "Output",
};
static const int kN = (int)(sizeof(kNames) / sizeof(kNames[0]));

int main() {
    char msg[256];
    const char* path = "test_set.tgset";

    float wrote[kN] = { 0.62f, 745.f, 88.5f, -7.f, 0.44f, 19.f, -4.5f };
    std::string paths[4] = { "C:\\tapes\\side b.wav", "", "C:\\stems\\drums.wav", "" };
    ok("saves", tg::savePreset(path, kNames, wrote, kN, paths, 4));

    // ---- round trip ----
    {
        std::map<std::string, double> got;
        std::map<int, std::string> samples;
        bool r = tg::loadPreset(path,
            [&](const std::string& n, double v) { got[n] = v; },
            [&](int slot, const std::string& s) { samples[slot] = s; });

        bool all = r && (int)got.size() == kN;
        for (int i = 0; i < kN && all; i++)
            if (std::fabs(got[kNames[i]] - wrote[i]) > 1e-3) all = false;
        snprintf(msg, sizeof msg, "%d of %d values", (int)got.size(), kN);
        ok("every value comes back as written", all, msg);

        snprintf(msg, sizeof msg, "%d slots: A=\"%s\" C=\"%s\"",
                 (int)samples.size(), samples[0].c_str(), samples[2].c_str());
        ok("each stem slot comes back in its own place, spaces intact",
           samples.size() == 2 &&
           samples[0] == "C:\\tapes\\side b.wav" &&
           samples[2] == "C:\\stems\\drums.wav", msg);
    }

    // ---- a set written before stem slots existed ----
    {
        FILE* f = fopen("test_v1.tgset", "w");
        fprintf(f, "%s\nP|Position|0.400000\nS|C:\\old\\tape.wav\n", tg::kPresetMagic);
        fclose(f);

        std::map<int, std::string> samples;
        bool r = tg::loadPreset("test_v1.tgset",
            [](const std::string&, double) {},
            [&](int slot, const std::string& s) { samples[slot] = s; });
        snprintf(msg, sizeof msg, "slot %d -> \"%s\"",
                 samples.empty() ? -1 : samples.begin()->first,
                 samples.empty() ? "" : samples.begin()->second.c_str());
        ok("an old single-sample set lands in slot A",
           r && samples.size() == 1 && samples[0] == "C:\\old\\tape.wav", msg);
    }

    // ---- a set from a build that had a parameter this one does not ----
    {
        FILE* f = fopen("test_future.tgset", "w");
        fprintf(f, "%s\n", tg::kPresetMagic);
        fprintf(f, "P|Position|0.300000\n");
        fprintf(f, "P|Reverb Size|0.900000\n");     // never existed here
        fprintf(f, "P|Level|0.250000\n");
        fclose(f);

        std::map<std::string, double> got;
        int unknown = 0;
        bool r = tg::loadPreset("test_future.tgset",
            [&](const std::string& n, double v) {
                bool known = false;
                for (int i = 0; i < kN; i++) if (n == kNames[i]) known = true;
                if (known) got[n] = v; else unknown++;
            },
            [&](int, const std::string&) {});
        snprintf(msg, sizeof msg, "%d known, %d unknown skipped",
                 (int)got.size(), unknown);
        ok("an unknown parameter is skipped, the rest still load",
           r && got.size() == 2 && unknown == 1 &&
           std::fabs(got["Position"] - 0.3) < 1e-6, msg);
    }

    // ---- a set from an older build, missing parameters this one has ----
    {
        // engine starts at its defaults; only what the file mentions moves
        tg::TapeGrain g;
        g.setSampleRate(44100);
        const float agentsBefore = g.getParam(tg::kAgentCount);

        FILE* f = fopen("test_old.tgset", "w");
        fprintf(f, "%s\n", tg::kPresetMagic);
        fprintf(f, "P|Position|0.800000\n");
        fclose(f);

        tg::loadPreset("test_old.tgset",
            [&](const std::string& n, double v) {
                if (n == "Position") g.setParam(tg::kPosition, (float)v);
                if (n == "Agents")   g.setParam(tg::kAgentCount, (float)v);
            },
            [&](int, const std::string&) {});

        snprintf(msg, sizeof msg, "Position %.2f, Agents still %.0f",
                 g.getParam(tg::kPosition), g.getParam(tg::kAgentCount));
        ok("a parameter the file never mentioned keeps its value",
           std::fabs(g.getParam(tg::kPosition) - 0.8f) < 1e-6 &&
           g.getParam(tg::kAgentCount) == agentsBefore, msg);
    }

    // ---- things that should fail cleanly rather than crash ----
    {
        bool missing = tg::loadPreset("no_such_file.tgset",
            [](const std::string&, double) {}, [](int, const std::string&) {});
        ok("a missing file fails cleanly", !missing);

        FILE* f = fopen("test_junk.tgset", "w");
        fprintf(f, "this is not a preset\nP|Position|0.5\n");
        fclose(f);
        int calls = 0;
        bool junk = tg::loadPreset("test_junk.tgset",
            [&](const std::string&, double) { calls++; },
            [](int, const std::string&) {});
        snprintf(msg, sizeof msg, "%d values read", calls);
        ok("a file that is not a set is refused, not half-applied",
           !junk && calls == 0, msg);

        // truncated mid-line
        f = fopen("test_trunc.tgset", "w");
        fprintf(f, "%s\nP|Position|0.5\nP|Grain\n P|\n", tg::kPresetMagic);
        fclose(f);
        calls = 0;
        bool trunc = tg::loadPreset("test_trunc.tgset",
            [&](const std::string&, double) { calls++; },
            [](int, const std::string&) {});
        snprintf(msg, sizeof msg, "%d values read", calls);
        ok("a truncated line is skipped without taking the file down",
           trunc && calls == 1, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
