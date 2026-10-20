// Read every cartridge in a folder and report what is in it.
//
// The reader was checked against a cartridge I built myself, which proves the
// packer and the unpacker agree but not that either matches a real Yamaha
// dump. This runs it over genuine ones: every voice of every file is decoded,
// rendered through msfa, and checked for a plausible name and a sound.

#include "dx_sysex.h"
#include "dx_engine.h"

#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: scan_carts <folder>\n"); return 2; }

    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(argv[1], ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".syx") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) { printf("no .syx found in %s\n", argv[1]); return 1; }

    mdx::DxEngine eng;
    eng.setSampleRate(44100.0);

    int carts = 0, refused = 0, voices = 0, silent = 0, badName = 0, notFinite = 0;
    std::vector<std::string> refusedNames;

    for (const auto& f : files) {
        std::vector<mdx::SysexVoice> v;
        const int n = mdx::loadSysex(f.string().c_str(), v);
        if (n != mdx::kVoicesPerCart) {
            refused++;
            refusedNames.push_back(f.filename().string());
            continue;
        }
        carts++;
        voices += n;

        for (const auto& sv : v) {
            // a voice name should be printable ASCII
            for (const char* p = sv.name; *p; p++)
                if (*p < 32 || *p > 126) { badName++; break; }

            eng.allOff();
            eng.noteOn(sv.data, 60, 99, 0.5f, 1.f, 0);
            std::vector<float> l(6000, 0.f), r(6000, 0.f);
            eng.render(l.data(), r.data(), 6000);
            double pk = 0;
            bool fin = true;
            for (size_t i = 0; i < l.size(); i++) {
                if (!std::isfinite(l[i])) fin = false;
                pk = std::max(pk, (double)std::fabs(l[i]));
            }
            if (!fin) notFinite++;
            if (pk < 1e-6) silent++;
        }
    }

    printf("%d cartridges read, %d refused, %d voices decoded\n",
           carts, refused, voices);
    printf("  %d voices with a non-printable name\n", badName);
    printf("  %d voices that made no sound\n", silent);
    printf("  %d voices that produced NaN or inf\n", notFinite);
    if (!refusedNames.empty()) {
        printf("  refused:");
        for (size_t i = 0; i < refusedNames.size() && i < 8; i++)
            printf(" %s", refusedNames[i].c_str());
        printf("\n");
    }

    // a sample of what was found, so the names can be eyeballed
    if (!files.empty()) {
        std::vector<mdx::SysexVoice> v;
        if (mdx::loadSysex(files.front().string().c_str(), v) > 0) {
            printf("\n%s:\n ", files.front().filename().string().c_str());
            for (int i = 0; i < 16 && i < (int)v.size(); i++)
                printf(" %-11s%s", v[i].name, (i % 4 == 3) ? "\n " : "");
            printf("\n");
        }
    }
    return (refused || notFinite) ? 1 : 0;
}
