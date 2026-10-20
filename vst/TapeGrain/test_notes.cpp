// The instrument side: voice allocation, the four pad modes, sync, and the
// per-grain window. These are the behaviours the Move module's README states
// as measured, so they are worth checking against rather than assuming.

#include "tapegrain_dsp.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const char* x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x[0] ? "  " : "", x);
    if (!c) fails++;
}

static const int SR = 44100;

// Four zones of distinct pitch, so Spread mode can be checked by ear-proxy:
// the dominant frequency of what comes out tells you where the head was.
static std::vector<float> zonedSource(int seconds) {
    const int len = SR * seconds;
    std::vector<float> s(len);
    const double f[4] = {200, 600, 1000, 1400};
    for (int i = 0; i < len; i++) {
        int z = (int)((double)i / len * 4);
        if (z > 3) z = 3;
        s[i] = 0.7f * (float)std::sin(2 * 3.14159265358979 * f[z] * i / SR);
    }
    return s;
}

// Zero-crossing rate of the output is a cheap stand-in for pitch.
static double zcr(const float* b, int n) {
    int c = 0;
    for (int i = 1; i < n; i++) if ((b[i] < 0) != (b[i - 1] < 0)) c++;
    return (double)c / n * SR * 0.5;
}

static double renderZcr(tg::TapeGrain& g, int blocks, double* peakOut = nullptr) {
    const int N = 512;
    float l[N], r[N];
    std::vector<float> all;
    double pk = 0;
    for (int b = 0; b < blocks; b++) {
        g.process(l, r, N);
        for (int i = 0; i < N; i++) { all.push_back(l[i]); pk = std::max(pk, (double)std::fabs(l[i])); }
    }
    if (peakOut) *peakOut = pk;
    return zcr(all.data(), (int)all.size());
}

int main() {
    char msg[160];
    std::vector<float> src = zonedSource(4);

    tg::TapeGrain g;
    g.setSampleRate(SR);
    g.setSample(src.data(), (int)src.size());
    g.setParam(tg::kMotion, (float)tg::mStatic);
    g.setParam(tg::kSize, 200.f);
    g.setParam(tg::kDensity, 40.f);
    g.setParam(tg::kSpray, 0.f);
    g.setParam(tg::kAttack, 5.f);
    g.setParam(tg::kRelease, 20.f);

    // ---- polyphony ----
    g.setParam(tg::kVoicing, (float)tg::vPoly);
    for (int i = 0; i < 6; i++) g.noteOn(60 + i, 1.f);
    snprintf(msg, sizeof msg, "%d voices", g.activeVoices());
    ok("six notes hold six voices", g.activeVoices() == 6, msg);

    g.noteOn(72, 1.f);          // a seventh must steal, not be dropped
    snprintf(msg, sizeof msg, "%d voices", g.activeVoices());
    ok("a seventh note steals rather than piling up", g.activeVoices() == 6, msg);

    for (int i = 0; i < 6; i++) g.noteOff(60 + i);
    g.noteOff(72);
    renderZcr(g, 60);
    snprintf(msg, sizeof msg, "%d voices", g.activeVoices());
    ok("all notes off frees every voice", g.activeVoices() == 0, msg);

    // ---- mono ----
    g.setParam(tg::kVoicing, (float)tg::vMono);
    g.noteOn(60, 1.f); g.noteOn(64, 1.f); g.noteOn(67, 1.f);
    renderZcr(g, 4);
    snprintf(msg, sizeof msg, "%d voices", g.activeVoices());
    ok("mono holds one voice at a time", g.activeVoices() == 1, msg);
    g.allNotesOff();
    renderZcr(g, 60);
    g.setParam(tg::kVoicing, (float)tg::vPoly);

    // ---- Pad Mode: Pitch transposes ----
    g.setParam(tg::kPadMode, (float)tg::padPitch);
    g.setParam(tg::kPosition, 0.125f);          // sits inside the 200 Hz zone
    g.noteOn(60, 1.f);
    double root = renderZcr(g, 40);
    g.allNotesOff(); renderZcr(g, 60);
    g.noteOn(72, 1.f);                          // one octave up
    double oct = renderZcr(g, 40);
    g.allNotesOff(); renderZcr(g, 60);
    snprintf(msg, sizeof msg, "%.0f Hz -> %.0f Hz", root, oct);
    ok("Pitch mode: an octave up roughly doubles the pitch",
       oct > root * 1.6 && oct < root * 2.4, msg);

    // ---- Pad Mode: Spread walks the buffer at original pitch ----
    g.setParam(tg::kPadMode, (float)tg::padSpread);
    g.setParam(tg::kSpreadKeys, 4.f);           // one key per zone
    double zone[4];
    for (int k = 0; k < 4; k++) {
        g.noteOn(60 + k, 1.f);
        zone[k] = renderZcr(g, 40);
        g.allNotesOff();
        renderZcr(g, 60);
    }
    snprintf(msg, sizeof msg, "%.0f, %.0f, %.0f, %.0f Hz",
             zone[0], zone[1], zone[2], zone[3]);
    ok("Spread mode: keys walk through the tape in order",
       zone[0] < zone[1] && zone[1] < zone[2] && zone[2] < zone[3], msg);

    // and wraps, so key 4 sounds like key 0
    g.noteOn(64, 1.f);
    double wrapped = renderZcr(g, 40);
    g.allNotesOff(); renderZcr(g, 60);
    snprintf(msg, sizeof msg, "%.0f vs %.0f Hz", wrapped, zone[0]);
    ok("Spread mode wraps past the last key",
       std::fabs(wrapped - zone[0]) < zone[0] * 0.35, msg);

    // ---- Pad Mode: Slice keeps original pitch ----
    {
        // Five loud hits with four quiet ghost notes between them - the same
        // shape of material the module's sensitivity table was measured on.
        // Without the ghosts there is nothing for sensitivity to reveal and
        // the test would pass while proving nothing.
        std::vector<float> hits(SR * 3, 0.f);
        uint32_t rs = 999;
        auto burst = [&](double at, float amp) {
            int start = (int)(SR * at);
            for (int i = 0; i < SR * 0.1 && start + i < (int)hits.size(); i++) {
                rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
                hits[start + i] += amp * (float)((int32_t)rs / 2147483648.0)
                                 * (float)std::exp(-i / (SR * 0.02));
            }
        };
        for (int h = 0; h < 5; h++) burst(0.2 + h * 0.5, 1.0f);
        for (int h = 0; h < 4; h++) burst(0.45 + h * 0.5, 0.12f);
        tg::TapeGrain s;
        s.setSampleRate(SR);
        s.setParam(tg::kSensitivity, 0.5f);
        s.setSample(hits.data(), (int)hits.size());
        snprintf(msg, sizeof msg, "%d slices from 5 hits", s.sliceCount());
        ok("onset detection finds the hits", s.sliceCount() >= 4, msg);

        // sensitivity must move the count in the right direction
        s.setParam(tg::kSensitivity, 0.f);
        s.refreshSlices();
        int few = s.sliceCount();
        s.setParam(tg::kSensitivity, 1.f);
        s.refreshSlices();
        int many = s.sliceCount();
        snprintf(msg, sizeof msg, "%d at 0.0, %d at 1.0", few, many);
        ok("sensitivity finds the ghost notes the low setting ignores",
           many > few && few >= 4, msg);

        s.setParam(tg::kSensitivity, 0.5f);
        s.refreshSlices();
        s.setParam(tg::kPadMode, (float)tg::padSlice);
        s.setParam(tg::kMotion, (float)tg::mStatic);
        s.noteOn(60, 1.f);
        double pk = 0;
        renderZcr(s, 40, &pk);
        snprintf(msg, sizeof msg, "peak %.3f", pk);
        ok("Slice mode plays from a detected onset", pk > 1e-4, msg);
    }

    // ---- sync ----
    {
        tg::TapeGrain s;
        s.setSampleRate(SR);
        s.setSample(src.data(), (int)src.size());
        s.setParam(tg::kMotion, (float)tg::mScan);
        s.setParam(tg::kScan, 1.f);
        s.setParam(tg::kSync, (float)tg::syncInternal);
        s.setParam(tg::kTempo, 120.f);
        s.setParam(tg::kRate, (float)tg::r1_16);
        s.setParam(tg::kPosition, 0.5f);
        s.noteOn(60, 1.f);

        // 120 BPM at 1/16 is 8 steps a second; the head is pulled back to
        // Position on every one, so it can never wander far
        const int N = 512;
        float l[N], r[N];
        double maxDev = 0;
        for (int b = 0; b < 86 * 2; b++) {
            s.process(l, r, N);
            maxDev = std::max(maxDev, std::fabs(s.playhead() - 0.5));
        }
        snprintf(msg, sizeof msg, "max deviation %.4f of the buffer", maxDev);
        ok("Internal sync keeps retriggering to Position", maxDev < 0.08, msg);

        s.setParam(tg::kSync, (float)tg::syncOff);
        double freeDev = 0;
        for (int b = 0; b < 86 * 2; b++) {
            s.process(l, r, N);
            freeDev = std::max(freeDev, std::fabs(s.playhead() - 0.5));
        }
        snprintf(msg, sizeof msg, "max deviation %.4f", freeDev);
        ok("with sync off the head runs free", freeDev > maxDev, msg);
    }

    // ---- grain window shape ----
    {
        // Isolate exactly one grain: 500 ms long, one every two seconds, and
        // render only that first 500 ms. Rendering longer lets a second grain
        // start and the argmax then reports its onset instead of the window.
        const double grainMs = 500.0;
        const int N = 512;
        const int blocks = (int)(SR * grainMs * 0.001) / N;   // stops inside grain 1

        double peakAt[3] = {0, 0, 0};
        const float shapes[3] = {0.0f, 0.5f, 1.0f};
        for (int k = 0; k < 3; k++) {
            tg::TapeGrain s;
            s.setSampleRate(SR);
            s.setSample(src.data(), (int)src.size());
            s.setParam(tg::kMotion, (float)tg::mStatic);
            s.setParam(tg::kPosition, 0.5f);
            s.setParam(tg::kSize, (float)grainMs);
            s.setParam(tg::kDensity, 0.5f);
            s.setParam(tg::kSpray, 0.f);
            s.setParam(tg::kPanSpray, 0.f);
            s.setParam(tg::kAttack, 1.f);      // voice envelope out of the way
            s.setParam(tg::kShape, shapes[k]);
            s.noteOn(60, 1.f);

            std::vector<float> all;
            for (int b = 0; b < blocks; b++) {
                float l[N], r[N];
                s.process(l, r, N);
                for (int i = 0; i < N; i++) all.push_back(std::fabs(l[i]));
            }
            // the source is a sine, so smooth out its own zero crossings
            // before looking for where the window peaks
            const int w = 441;                 // 10 ms
            int best = 0; double bestV = -1;
            for (int i = 0; i + w < (int)all.size(); i += 64) {
                double m = 0;
                for (int j = 0; j < w; j++) m = std::max(m, (double)all[i + j]);
                if (m > bestV) { bestV = m; best = i; }
            }
            peakAt[k] = (double)best / all.size();
        }
        snprintf(msg, sizeof msg, "swell %.2f, Hann %.2f, percussive %.2f",
                 peakAt[0], peakAt[1], peakAt[2]);
        ok("Shape morphs the window from centred swell to instant attack",
           peakAt[1] > 0.35 && peakAt[1] < 0.65 && peakAt[2] < 0.15, msg);
    }

    // ---- flock size ----
    {
        tg::TapeGrain s;
        s.setSampleRate(SR);
        s.setSample(src.data(), (int)src.size());
        s.setParam(tg::kMotion, (float)tg::mSwarm);
        s.setParam(tg::kCohesion, 0.f);      // spread out, so a count is visible
        s.setParam(tg::kDrift, 0.5f);

        int seen[2] = {0, 0};
        const float counts[2] = {3.f, 20.f};
        for (int k = 0; k < 2; k++) {
            s.setParam(tg::kAgentCount, counts[k]);
            s.allNotesOff();
            renderZcr(s, 40);
            s.noteOn(60, 1.f);
            renderZcr(s, 40);
            seen[k] = s.snapshot().numAgents;
            s.allNotesOff();
            renderZcr(s, 60);
        }
        snprintf(msg, sizeof msg, "%d and %d agents reported", seen[0], seen[1]);
        ok("the flock size follows the parameter", seen[0] == 3 && seen[1] == 20, msg);

        // out of range must clamp, not read past the pool
        s.setParam(tg::kAgentCount, 999.f);
        s.noteOn(60, 1.f);
        renderZcr(s, 10);
        snprintf(msg, sizeof msg, "%d agents", s.snapshot().numAgents);
        ok("an out-of-range flock size clamps to the pool",
           s.snapshot().numAgents == tg::kMaxAgents, msg);
        s.allNotesOff();
    }

    // ---- output trim ----
    {
        tg::TapeGrain s;
        s.setSampleRate(SR);
        s.setSample(src.data(), (int)src.size());
        s.setParam(tg::kMotion, (float)tg::mStatic);
        s.noteOn(60, 1.f);

        double unity = 0, boosted = 0, cut = 0, muted = 0;
        renderZcr(s, 30, &unity);
        s.setParam(tg::kOutput, 6.f);
        renderZcr(s, 30, &boosted);
        s.setParam(tg::kOutput, -12.f);
        renderZcr(s, 30, &cut);
        s.setParam(tg::kOutput, -60.f);
        renderZcr(s, 30, &muted);

        snprintf(msg, sizeof msg, "0 dB %.3f, +6 dB %.3f, -12 dB %.3f",
                 unity, boosted, cut);
        ok("Output trims and boosts the whole signal",
           boosted > unity * 1.6 && cut < unity * 0.5, msg);
        snprintf(msg, sizeof msg, "%.8f", muted);
        ok("Output at the bottom of its range is silence", muted < 1e-9, msg);
    }

    // ---- stem slots ----
    {
        // two clearly different sources, so which slot is sounding can be
        // told apart by ear-proxy rather than taken on trust
        const int len = SR * 2;
        std::vector<float> lowTone(len), highTone(len);
        for (int i = 0; i < len; i++) {
            lowTone[i]  = 0.7f * (float)std::sin(2 * 3.14159265358979 * 200 * i / SR);
            highTone[i] = 0.7f * (float)std::sin(2 * 3.14159265358979 * 1400 * i / SR);
        }

        tg::TapeGrain s;
        s.setSampleRate(SR);
        s.setParam(tg::kMotion, (float)tg::mStatic);
        s.setParam(tg::kPosition, 0.5f);
        s.setParam(tg::kSpray, 0.f);
        s.setParam(tg::kSize, 200.f);
        s.setParam(tg::kDensity, 40.f);

        s.setSample(0, lowTone.data(), len);
        s.setSample(2, highTone.data(), len);
        snprintf(msg, sizeof msg, "0:%d 1:%d 2:%d 3:%d",
                 s.slotFilled(0), s.slotFilled(1), s.slotFilled(2), s.slotFilled(3));
        ok("slots fill independently", s.slotFilled(0) && !s.slotFilled(1) &&
           s.slotFilled(2) && !s.slotFilled(3), msg);

        // only A up
        s.setParam(tg::kStemA, 1.f); s.setParam(tg::kStemB, 0.f);
        s.setParam(tg::kStemC, 0.f); s.setParam(tg::kStemD, 0.f);
        s.noteOn(60, 1.f);
        double a = renderZcr(s, 40);

        // the switch happens under a held note: nothing is reloaded, so it
        // has to take effect without a gap
        s.setParam(tg::kStemA, 0.f); s.setParam(tg::kStemC, 1.f);
        double pk = 0;
        double b = renderZcr(s, 40, &pk);
        snprintf(msg, sizeof msg, "%.0f Hz -> %.0f Hz, peak %.3f", a, b, pk);
        ok("a stem swap takes effect under a held note",
           b > a * 1.8 && pk > 1e-4, msg);

        // both at once: the low tone is back alongside the high one, so the
        // crossing rate lands between the two
        s.setParam(tg::kStemA, 1.f);
        double both = 0;
        double mixed = renderZcr(s, 40, &both);
        snprintf(msg, sizeof msg, "A %.0f Hz, C %.0f Hz, together %.0f Hz",
                 a, b, mixed);
        ok("two stems play together rather than replacing each other",
           mixed < b * 0.95 && both > 1e-4, msg);

        // muting every stem is silence, and nothing keeps leaking through
        s.setParam(tg::kStemA, 0.f); s.setParam(tg::kStemC, 0.f);
        double empty = 0;
        renderZcr(s, 40, &empty);
        snprintf(msg, sizeof msg, "peak %.8f", empty);
        ok("with every stem down it is silent", empty < 1e-9, msg);

        // an empty slot turned up contributes nothing and does not crash
        s.setParam(tg::kStemB, 1.f);
        double onlyEmpty = 0;
        renderZcr(s, 40, &onlyEmpty);
        snprintf(msg, sizeof msg, "peak %.8f", onlyEmpty);
        ok("turning up a slot with nothing in it is safe", onlyEmpty < 1e-9, msg);
        s.setParam(tg::kStemB, 0.f);
        s.setParam(tg::kStemA, 1.f);

        // each slot keeps its own analysis, reached through the view focus
        s.setParam(tg::kStem, 0.f);
        int slicesA = s.sliceCount();
        s.setParam(tg::kStem, 2.f);
        int slicesC = s.sliceCount();
        snprintf(msg, sizeof msg, "slot 0: %d, slot 2: %d", slicesA, slicesC);
        ok("each slot carries its own slices", slicesA > 0 && slicesC > 0, msg);
        s.allNotesOff();
    }

    // ---- grain count ----
    {
        tg::TapeGrain s;
        s.setSampleRate(SR);
        s.setSample(src.data(), (int)src.size());
        s.setParam(tg::kMotion, (float)tg::mStatic);
        s.setParam(tg::kSize, 400.f);      // long grains, so many overlap
        s.setParam(tg::kDensity, 120.f);   // ask for far more than the low cap
        s.setParam(tg::kAttack, 1.f);

        int seen[2] = {0, 0};
        const float counts[2] = {8.f, 200.f};
        for (int k = 0; k < 2; k++) {
            s.allNotesOff();
            renderZcr(s, 60);
            s.setParam(tg::kGrainCount, counts[k]);
            s.noteOn(60, 1.f);
            int peakGrains = 0;
            for (int b = 0; b < 60; b++) {
                float l[512], r[512];
                s.process(l, r, 512);
                peakGrains = std::max(peakGrains, s.snapshot().numGrains);
            }
            seen[k] = peakGrains;
        }
        snprintf(msg, sizeof msg, "%d grains at a cap of 8, %d at 200",
                 seen[0], seen[1]);
        ok("the grain cap holds, and raising it lets far more sound",
           seen[0] <= 8 && seen[1] > 40, msg);
    }

    // ---- presets ----
    {
        tg::TapeGrain s;
        s.setSampleRate(SR);
        float before = s.getParam(tg::kSize);
        float vals[tg::kNumParams];
        s.applyPreset(1, vals);            // Glitch: 45 ms grains, 1/16
        snprintf(msg, sizeof msg, "size %.0f -> %.0f ms", before, s.getParam(tg::kSize));
        ok("a preset writes its values in",
           s.getParam(tg::kSize) == 45.f &&
           s.getParam(tg::kRate) == (float)tg::r1_16, msg);

        // and leaves what you are playing alone
        s.setParam(tg::kPosition, 0.77f);
        s.setParam(tg::kLevel, 0.33f);
        s.applyPreset(3, vals);            // Cloud Pad
        snprintf(msg, sizeof msg, "position %.2f, level %.2f",
                 s.getParam(tg::kPosition), s.getParam(tg::kLevel));
        ok("a preset leaves position and level untouched",
           s.getParam(tg::kPosition) == 0.77f && s.getParam(tg::kLevel) == 0.33f, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
