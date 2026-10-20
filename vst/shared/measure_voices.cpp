// How loud is a cartridge voice, really?
//
// The engine trim was chosen against the patches derived from the eleven
// strokes - short, percussive, two operators doing most of the work. A factory
// DX7 voice is a different animal: six operators, often all carriers, held
// rather than struck. This renders every voice in the folder and reports the
// distribution, so the trim for imported voices is a measured number instead
// of a guess.

#include "cart_index.h"
#include "dx_engine.h"

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cmath>
#include <string>

static const double SR = 44100.0;

int main() {
    const char* base = std::getenv("LOCALAPPDATA");
    const std::string dir = std::string(base ? base : ".") + "\\MaghrebDX\\Cartridges";

    mdx::CartIndex ix;
    ix.scan(dir);
    if (ix.empty()) { printf("no cartridges\n"); return 1; }

    mdx::DxEngine eng;
    eng.setSampleRate(SR);

    std::vector<float> peaks;
    std::string loudestName;
    float loudest = 0.f;

    const int N = 256;
    std::vector<float> L(N), R(N);

    for (int c = 0; c < ix.count(); c++) {
        std::vector<mdx::SysexVoice> v;
        if (mdx::loadSysex(ix.cart(c).path.c_str(), v) <= 0) continue;
        for (int i = 0; i < (int)v.size(); i++) {
            eng.allOff();
            // note 36 is where the drums sit; gain 1.0 so the number is the
            // engine's own output, not a level setting
            eng.noteOn(v[i].data, 36, 127, 0.5f, 1.0f, 0);
            float pk = 0.f;
            for (int b = 0; b < 120; b++) {          // ~0.7 s
                std::fill(L.begin(), L.end(), 0.f);
                std::fill(R.begin(), R.end(), 0.f);
                eng.render(L.data(), R.data(), N);
                for (int k = 0; k < N; k++) pk = std::max(pk, std::fabs(L[k]) + std::fabs(R[k]));
            }
            peaks.push_back(pk);
            if (pk > loudest) { loudest = pk; loudestName = ix.cart(c).name + " / " + v[i].name; }
        }
    }

    std::sort(peaks.begin(), peaks.end());
    auto pct = [&](double p) { return peaks[(size_t)(p * (peaks.size() - 1))]; };
    printf("%d voices rendered\n", (int)peaks.size());
    printf("  median %.3f   p90 %.3f   p99 %.3f   max %.3f\n",
           pct(0.5), pct(0.9), pct(0.99), peaks.back());
    printf("  loudest: %s\n", loudestName.c_str());
    printf("  trim for a 0.90 ceiling at the loudest: %.4f\n", 0.90 / peaks.back());
    printf("  trim for a 0.90 ceiling at p99        : %.4f\n", 0.90 / pct(0.99));
    int over = 0;
    for (float p : peaks) if (p > 1.0f) over++;
    printf("  %d of %d already clip at gain 1.0\n", over, (int)peaks.size());
    return 0;
}
