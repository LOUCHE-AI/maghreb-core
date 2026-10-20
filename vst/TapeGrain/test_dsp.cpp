// Render the TapeGrain engine offline and check what comes out.
//
// This is the part that could never be done for the gen~ version: compile the
// DSP, feed it a known sample, and measure the audio before it goes anywhere
// near a host.

#include "tapegrain_dsp.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* label, bool cond, const char* extra = "") {
    printf("%s  %s%s%s\n", cond ? "PASS " : "FAIL ", label,
           extra[0] ? "  " : "", extra);
    if (!cond) fails++;
}

static void writeWav(const std::string& path, const std::vector<float>& l,
                     const std::vector<float>& r, int sr) {
    size_t n = l.size();
    std::vector<int16_t> inter(n * 2);
    for (size_t i = 0; i < n; i++) {
        float a = l[i] < -1 ? -1 : (l[i] > 1 ? 1 : l[i]);
        float b = r[i] < -1 ? -1 : (r[i] > 1 ? 1 : r[i]);
        inter[i * 2]     = (int16_t)(a * 32767);
        inter[i * 2 + 1] = (int16_t)(b * 32767);
    }
    uint32_t dataBytes = (uint32_t)(inter.size() * 2);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    uint32_t u; uint16_t s;
    fwrite("RIFF", 1, 4, f); u = 36 + dataBytes; fwrite(&u, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); u = 16; fwrite(&u, 4, 1, f);
    s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    u = sr; fwrite(&u, 4, 1, f); u = sr * 4; fwrite(&u, 4, 1, f);
    s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(inter.data(), 1, dataBytes, f);
    fclose(f);
}

int main() {
    const int SR = 44100;

    // a source with four clear transients, so slicing has something real to find
    const int LEN = SR * 2;
    std::vector<float> src(LEN, 0.f);
    const double bursts[4] = {0.05, 0.30, 0.55, 0.80};
    uint32_t rs = 12345;
    for (int b = 0; b < 4; b++) {
        int start = (int)(bursts[b] * LEN);
        for (int i = 0; i < SR * 0.12 && start + i < LEN; i++) {
            rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
            float w = (float)((int32_t)rs / 2147483648.0);
            src[start + i] = w * (float)std::exp(-i / (SR * 0.03));
        }
    }

    tg::TapeGrain g;
    g.setSampleRate(SR);

    // no sample: must be silent and must not crash
    {
        std::vector<float> L(512), R(512);
        g.noteOn(60, 1.f);
        g.process(L.data(), R.data(), 512);
        float pk = 0; for (int i = 0; i < 512; i++) pk = std::max(pk, std::fabs(L[i]));
        ok("no sample renders silence", pk == 0.f);
        g.allNotesOff();
    }

    g.setSample(src.data(), LEN);
    ok("sample accepted", g.hasSample());

    char msg[128];
    snprintf(msg, sizeof msg, "%d slices", g.sliceCount());
    ok("onsets detected", g.sliceCount() >= 3, msg);

    // It is an instrument now: with a sample loaded but nothing held it must
    // still be silent, which is the module's own behaviour.
    {
        float bl[512], br[512];
        double pk = 0;
        for (int b = 0; b < 20; b++) {
            g.process(bl, br, 512);
            for (int i = 0; i < 512; i++) pk = std::max(pk, (double)std::fabs(bl[i]));
        }
        snprintf(msg, sizeof msg, "peak %.6f", pk);
        ok("silent at rest with no note held", pk < 1e-9, msg);
    }

    // render each motion mode and measure
    const char* names[8] = {"Static", "Scan", "PingPong", "Walk",
                            "Step", "Similar", "Swarm", "PitchSwarm"};
    bool allSound = true, allFinite = true;
    for (int m = 0; m < 8; m++) {
        g.setParam(tg::kMotion, (float)m);
        g.setParam(tg::kPosition, 0.3f);
        g.setParam(tg::kSpan, 0.3f);
        g.setParam(tg::kDensity, 40.f);
        g.setParam(tg::kSize, 150.f);
        g.allNotesOff();
        g.noteOn(60, 1.f);                  // C3 = 0 st, holds for the render

        const int blocks = 86;              // ~1 second at 512
        std::vector<float> L, R;
        float bl[512], br[512];
        double peak = 0, rms = 0;
        double headMin = 1, headMax = 0;
        for (int b = 0; b < blocks; b++) {
            g.process(bl, br, 512);
            for (int i = 0; i < 512; i++) {
                if (!std::isfinite(bl[i]) || !std::isfinite(br[i])) allFinite = false;
                peak = std::max(peak, (double)std::fabs(bl[i]));
                rms += (double)bl[i] * bl[i];
                L.push_back(bl[i]); R.push_back(br[i]);
            }
            headMin = std::min(headMin, g.playhead());
            headMax = std::max(headMax, g.playhead());
        }
        rms = std::sqrt(rms / (blocks * 512));
        if (peak < 1e-4) allSound = false;

        snprintf(msg, sizeof msg, "%-10s peak=%.3f rms=%.4f head %.2f..%.2f",
                 names[m], peak, rms, headMin, headMax);
        ok("renders audio", peak > 1e-4, msg);

        writeWav(std::string("render_") + names[m] + ".wav", L, R, SR);
    }
    ok("every motion mode produced sound", allSound);
    ok("no NaN or inf anywhere", allFinite);

    // pitch must actually change the read rate
    {
        g.setParam(tg::kMotion, 0.f);
        g.setParam(tg::kPitch, 12.f);
        float bl[512], br[512];
        double peak = 0;
        for (int b = 0; b < 40; b++) {
            g.process(bl, br, 512);
            for (int i = 0; i < 512; i++) peak = std::max(peak, (double)std::fabs(bl[i]));
        }
        ok("pitched playback still sounds", peak > 1e-4);
        g.setParam(tg::kPitch, 0.f);
    }

    // note off must actually stop it, and only after the release has run
    {
        g.setParam(tg::kRelease, 200.f);
        float bl[512], br[512];
        g.noteOff(60);
        double during = 0;
        for (int b = 0; b < 8; b++) {        // ~93 ms, inside the release
            g.process(bl, br, 512);
            for (int i = 0; i < 512; i++) during = std::max(during, (double)std::fabs(bl[i]));
        }
        for (int b = 0; b < 120; b++) g.process(bl, br, 512);
        double after = 0;
        for (int b = 0; b < 20; b++) {
            g.process(bl, br, 512);
            for (int i = 0; i < 512; i++) after = std::max(after, (double)std::fabs(bl[i]));
        }
        snprintf(msg, sizeof msg, "%.4f during release, %.6f after", during, after);
        ok("note off releases, then frees the voice",
           during > 1e-4 && after < 1e-9 && g.activeVoices() == 0, msg);
        g.setParam(tg::kRelease, 600.f);
        g.noteOn(60, 1.f);
    }

    // freeze must hold the playhead
    {
        g.setParam(tg::kMotion, (float)tg::mScan);
        g.setParam(tg::kFreeze, 1.f);
        float bl[512], br[512];
        g.process(bl, br, 512);
        double h0 = g.playhead();
        for (int b = 0; b < 20; b++) g.process(bl, br, 512);
        snprintf(msg, sizeof msg, "%.6f -> %.6f", h0, g.playhead());
        ok("freeze holds the playhead", std::fabs(g.playhead() - h0) < 1e-9, msg);
        g.setParam(tg::kFreeze, 0.f);
    }

    // level scales the output
    {
        g.setParam(tg::kMotion, 0.f);
        float bl[512], br[512];
        double loud = 0, quiet = 0;
        g.setParam(tg::kLevel, 1.0f);
        for (int b = 0; b < 40; b++) { g.process(bl, br, 512);
            for (int i = 0; i < 512; i++) loud = std::max(loud, (double)std::fabs(bl[i])); }
        g.setParam(tg::kLevel, 0.25f);
        for (int b = 0; b < 40; b++) { g.process(bl, br, 512);
            for (int i = 0; i < 512; i++) quiet = std::max(quiet, (double)std::fabs(bl[i])); }
        snprintf(msg, sizeof msg, "%.3f -> %.3f", loud, quiet);
        ok("level scales the output", quiet < loud * 0.5, msg);
        g.setParam(tg::kLevel, 0.8f);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
