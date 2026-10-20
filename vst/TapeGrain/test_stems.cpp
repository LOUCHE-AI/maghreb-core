// Stem-set matching, checked against the real filenames Live produced on this
// machine as well as against synthetic ones. Naming conventions are exactly
// the kind of thing that looks obvious and then differs by a space.

#include "tapegrain_dsp.h"
#include "tapegrain_stems.h"
#include <cstdio>
#include <filesystem>
#include <string>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l,
           x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static std::string leaf(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

int main() {
    char msg[512];
    std::string got[tg::kStemSlots];

    // ---- synthetic, in a temp folder we control ----
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "tapegrain_stem_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);

        const std::string base = "loop [2025-09-19 180943] ";
        const char* parts[4] = {"Vocals", "Drums", "Bass", "Other"};
        for (int i = 0; i < 4; i++) {
            std::string f = (dir / (base + "(" + parts[i] + ").wav")).string();
            FILE* h = fopen(f.c_str(), "wb"); if (h) fclose(h);
        }

        // picking any one of them must find all four, in slot order
        for (int pick = 0; pick < 4; pick++) {
            std::string p = (dir / (base + "(" + std::string(parts[pick]) + ").wav")).string();
            int n = tg::findStemSet(p, got);
            bool order = (n == 4);
            for (int i = 0; i < 4 && order; i++)
                if (leaf(got[i]).find(std::string("(") + parts[i] + ")") == std::string::npos)
                    order = false;
            snprintf(msg, sizeof msg, "picked (%s), found %d", parts[pick], n);
            ok("any one of the four finds the whole set, in slot order", order, msg);
        }

        // a partial set: only what exists comes back, the rest stay empty
        fs::remove(dir / (base + "(Bass).wav"), ec);
        int n = tg::findStemSet((dir / (base + "(Drums).wav")).string(), got);
        snprintf(msg, sizeof msg, "found %d, Bass slot \"%s\"", n, got[2].c_str());
        ok("a missing part leaves its slot empty rather than shifting the rest",
           n == 3 && got[2].empty() && !got[0].empty() && !got[1].empty()
                  && !got[3].empty(), msg);

        // an ordinary sample is not a stem set
        std::string plain = (dir / "just a recording.wav").string();
        FILE* h = fopen(plain.c_str(), "wb"); if (h) fclose(h);
        ok("an ordinary file is not treated as a stem set",
           tg::findStemSet(plain, got) == 0);

        // brackets that are not a stem name
        std::string other = (dir / "take [2026-05-11 111503].wav").string();
        h = fopen(other.c_str(), "wb"); if (h) fclose(h);
        ok("a timestamp in brackets is not mistaken for a stem name",
           tg::findStemSet(other, got) == 0);

        ok("an empty path is handled", tg::findStemSet("", got) == 0);

        fs::remove_all(dir, ec);
    }

    // ---- demucs layout: the part is the whole filename ----
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "tapegrain_demucs_test"
                             / "htdemucs" / "some track";
        std::error_code ec;
        fs::remove_all(fs::temp_directory_path() / "tapegrain_demucs_test", ec);
        fs::create_directories(dir, ec);

        const char* lower[4] = {"vocals", "drums", "bass", "other"};
        for (int i = 0; i < 4; i++) {
            std::string f = (dir / (std::string(lower[i]) + ".wav")).string();
            FILE* h = fopen(f.c_str(), "wb"); if (h) fclose(h);
        }

        for (int pick = 0; pick < 4; pick++) {
            std::string pth = (dir / (std::string(lower[pick]) + ".wav")).string();
            int n = tg::findStemSet(pth, got);
            bool order = (n == 4);
            for (int i = 0; i < 4 && order; i++)
                if (leaf(got[i]) != std::string(lower[i]) + ".wav") order = false;
            snprintf(msg, sizeof msg, "picked %s.wav, found %d", lower[pick], n);
            ok("demucs output: any part finds the whole set, in slot order", order, msg);
        }

        // demucs can be told to write mp3 or flac instead
        const fs::path fdir = fs::temp_directory_path() / "tapegrain_demucs_test" / "flac";
        fs::create_directories(fdir, ec);
        for (int i = 0; i < 4; i++) {
            std::string f = (fdir / (std::string(lower[i]) + ".flac")).string();
            FILE* h = fopen(f.c_str(), "wb"); if (h) fclose(h);
        }
        ok("the extension is not assumed to be .wav",
           tg::findStemSet((fdir / "bass.flac").string(), got) == 4);

        // a partial run leaves the missing slot empty
        fs::remove(dir / "bass.wav", ec);
        int n = tg::findStemSet((dir / "drums.wav").string(), got);
        snprintf(msg, sizeof msg, "found %d, Bass slot \"%s\"", n, got[2].c_str());
        ok("a missing demucs part leaves its slot empty", n == 3 && got[2].empty(), msg);

        // a file that merely lives beside them is still not a stem
        std::string plain = (dir / "mixture.wav").string();
        FILE* h = fopen(plain.c_str(), "wb"); if (h) fclose(h);
        ok("a non-part file in a demucs folder is not a stem set",
           tg::findStemSet(plain, got) == 0);

        fs::remove_all(fs::temp_directory_path() / "tapegrain_demucs_test", ec);
    }

    // ---- the real thing, if it is still on this machine ----
    {
        const std::string real =
            "C:\\Users\\welal\\Desktop\\Bureaublad\\audio exp\\wooden wave Project"
            "\\Samples\\Processed\\Stems\\loop\\loop [2025-09-19 180943] (Drums).wav";
        std::error_code ec;
        if (std::filesystem::exists(real, ec)) {
            int n = tg::findStemSet(real, got);
            snprintf(msg, sizeof msg, "found %d: %s | %s | %s | %s", n,
                     leaf(got[0]).c_str(), leaf(got[1]).c_str(),
                     leaf(got[2]).c_str(), leaf(got[3]).c_str());
            ok("a real Live stem folder resolves to all four parts", n == 4, msg);
        } else {
            printf("SKIP   real Live stem folder not present on this machine\n");
        }
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
