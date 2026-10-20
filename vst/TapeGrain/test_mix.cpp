// Check the input path. Input Mix carries the module's meaning: 0 is grains
// only, 1 is the untouched input. As an instrument the default is 0, so this
// is about what happens when something IS routed in - and about Record, which
// is the only way material gets in without a file.

#include "tapegrain_dsp.h"
#include <cstdio>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const char* x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x[0] ? "  " : "", x);
    if (!c) fails++;
}

static double peakOf(const float* b, int n) {
    double p = 0;
    for (int i = 0; i < n; i++) p = std::max(p, (double)std::fabs(b[i]));
    return p;
}

int main() {
    const int N = 512;
    char msg[128];
    float inL[N], inR[N], oL[N], oR[N];
    for (int i = 0; i < N; i++) { inL[i] = 0.5f * sinf(i * 0.02f); inR[i] = inL[i]; }
    const double inPeak = peakOf(inL, N);

    tg::TapeGrain g;
    g.setSampleRate(44100);

    // fully wet with nothing loaded and nothing held: silence, correctly
    g.setParam(tg::kInputMix, 0.f);
    g.process(inL, inR, oL, oR, N);
    snprintf(msg, sizeof msg, "out %.4f", peakOf(oL, N));
    ok("no sample, no input: silent", peakOf(oL, N) < 1e-9, msg);

    // all the way to input, the source must arrive untouched
    g.setParam(tg::kInputMix, 1.f);
    g.process(inL, inR, oL, oR, N);
    snprintf(msg, sizeof msg, "in %.3f -> out %.3f", inPeak, peakOf(oL, N));
    ok("full input passes through unchanged",
       std::fabs(peakOf(oL, N) - inPeak) < 1e-6, msg);

    // half way is half the source
    g.setParam(tg::kInputMix, 0.5f);
    g.process(inL, inR, oL, oR, N);
    snprintf(msg, sizeof msg, "%.3f vs %.3f expected", peakOf(oL, N), inPeak * 0.5);
    ok("halfway passes half the source",
       std::fabs(peakOf(oL, N) - inPeak * 0.5) < 1e-4, msg);

    // now record the input and check the grain path appears
    g.setParam(tg::kInputMix, 0.f);
    g.setRecording(true, 1.0);
    for (int b = 0; b < 100; b++) {
        for (int i = 0; i < N; i++) {
            inL[i] = 0.5f * sinf((b * N + i) * 0.02f);
            inR[i] = inL[i];
        }
        g.captureInput(inL, inR, N);
        g.process(inL, inR, oL, oR, N);
    }
    g.setRecording(false);
    ok("recorded a take", g.hasSample());

    // a take alone is not enough - it is an instrument, so it needs a note
    double idle = 0;
    for (int b = 0; b < 20; b++) {
        g.process(nullptr, nullptr, oL, oR, N);
        idle = std::max(idle, peakOf(oL, N));
    }
    snprintf(msg, sizeof msg, "peak %.6f", idle);
    ok("a recorded take is still silent until played", idle < 1e-9, msg);

    g.setParam(tg::kMotion, (float)tg::mScan);
    g.noteOn(60, 1.f);
    double wet = 0;
    for (int b = 0; b < 40; b++) {
        g.process(nullptr, nullptr, oL, oR, N);
        wet = std::max(wet, peakOf(oL, N));
    }
    snprintf(msg, sizeof msg, "peak %.3f", wet);
    ok("held note produces grains from the take", wet > 1e-4, msg);

    // grains and the live input together
    g.setParam(tg::kInputMix, 0.5f);
    double both = 0;
    for (int b = 0; b < 40; b++) {
        g.process(inL, inR, oL, oR, N);
        both = std::max(both, peakOf(oL, N));
    }
    snprintf(msg, sizeof msg, "peak %.3f", both);
    ok("blend carries source and grains together", both > 1e-4, msg);

    // a null input must not crash the input path
    g.setParam(tg::kInputMix, 1.f);
    g.allNotesOff();
    for (int b = 0; b < 200; b++) g.process(nullptr, nullptr, oL, oR, N);
    ok("null input is safe", peakOf(oL, N) < 1e-9);

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
