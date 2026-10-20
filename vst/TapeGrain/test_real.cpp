// Load a real capture off the Move and granulate it. Synthetic tests can
// agree with a wrong assumption; this one uses a file the device actually
// produced (stereo, 48k, JUNK chunk before fmt).

#include "tapegrain_dsp.h"
#include "tapegrain_io.h"
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1]
        : "C:\\Users\\welal\\Desktop\\MOVE\\_extract\\sample-check.wav";
    const int SR = 44100;

    tg::WavData w = tg::loadWav(path);
    printf("file      : %s\n", path);
    if (!w.ok) { printf("FAILED to load\n"); return 1; }
    printf("channels  : %d\nrate      : %d Hz\nframes    : %zu (%.2f s)\n",
           w.channels, w.sampleRate, w.mono.size(),
           (double)w.mono.size() / w.sampleRate);

    float pk = 0;
    for (size_t i = 0; i < w.mono.size(); i++) pk = std::max(pk, std::fabs(w.mono[i]));
    printf("peak      : %.4f\n", pk);

    std::vector<float> s = tg::resample(w.mono, w.sampleRate, SR);
    printf("resampled : %zu frames at %d Hz\n", s.size(), SR);

    tg::TapeGrain g;
    g.setSampleRate(SR);
    g.setSample(s.data(), (int)s.size());
    printf("slices    : %d\n\n", g.sliceCount());

    const char* names[8] = {"Static", "Scan", "PingPong", "Walk",
                            "Step", "Similar", "Swarm", "PitchSwarm"};
    for (int m = 0; m < 8; m++) {
        g.setParam(tg::kMotion, (float)m);
        g.setParam(tg::kPosition, 0.3f);
        g.setParam(tg::kDensity, 45.f);
        g.setParam(tg::kSize, 140.f);
        g.allNotesOff();
        g.noteOn(60, 1.f);              // it is an instrument: hold a note

        std::vector<float> L, R;
        float bl[512], br[512];
        double peak = 0, rms = 0;
        for (int b = 0; b < 172; b++) {          // ~2 s
            g.process(bl, br, 512);
            for (int i = 0; i < 512; i++) {
                peak = std::max(peak, (double)std::fabs(bl[i]));
                rms += (double)bl[i] * bl[i];
                L.push_back(bl[i]); R.push_back(br[i]);
            }
        }
        rms = std::sqrt(rms / L.size());
        printf("  %-11s peak=%.3f rms=%.4f head=%.3f\n",
               names[m], peak, rms, g.playhead());

        // write it out so it can be listened to
        std::vector<float> mono(L.size());
        for (size_t i = 0; i < L.size(); i++) mono[i] = 0.5f * (L[i] + R[i]);
        tg::saveWav(std::string("move_") + names[m] + ".wav", mono, SR);
    }
    printf("\nrendered 8 files from a real Move capture\n");
    return 0;
}
