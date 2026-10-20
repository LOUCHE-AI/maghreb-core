// Splitting the stems apart on the way out must not change what comes out of
// the main. The filter in the way is linear, so filtering each stem and
// summing has to equal filtering the sum - this is where that gets checked
// rather than asserted.

#include "tapegrain_dsp.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& d = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, d.empty() ? "" : "  ", d.c_str());
    if (!c) fails++;
}
static constexpr double kPi = 3.14159265358979323846;
static const double SR = 48000;

// Four stems that are easy to tell apart afterwards.
static std::vector<float> stem(double hz, size_t n) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; i++) v[i] = 0.4f * (float)std::sin(2*kPi*hz*(double)i/SR);
    return v;
}

static void setup(tg::TapeGrain& g, bool split, int focus) {
    g.setSampleRate(SR);
    const size_t n = (size_t)(SR * 2);
    const double hz[4] = {110, 220, 440, 880};
    for (int s = 0; s < tg::kStemSlots; s++) {
        auto b = stem(hz[s], n);
        g.setSample(s, b.data(), (int)b.size());
        g.setParam(tg::kStemA + s, 1.f);
    }
    g.setParam(tg::kInputMix, 0.f);      // grains only
    g.setParam(tg::kFocus, (float)focus);
    g.setParam(tg::kMotion, (float)tg::mStatic);
    g.setSplitOutputs(split);
    g.noteOn(60, 1.0f);
}

int main() {
    char msg[256];

    for (int focus = 0; focus < tg::kNumFocus; focus++) {
        const char* fname[] = {"full", "low", "mid", "high"};
        const int blocks = 40, n = 256;

        std::vector<float> mixA, mixB, sum(0);
        std::vector<std::vector<float>> parts(tg::kStemSlots);

        { // without splitting
            tg::TapeGrain g; setup(g, false, focus);
            std::vector<float> L(n), R(n);
            for (int b = 0; b < blocks; b++) {
                g.process(nullptr, nullptr, L.data(), R.data(), n);
                mixA.insert(mixA.end(), L.begin(), L.end());
            }
        }
        { // with splitting
            tg::TapeGrain g; setup(g, true, focus);
            std::vector<float> L(n), R(n);
            for (int b = 0; b < blocks; b++) {
                g.process(nullptr, nullptr, L.data(), R.data(), n);
                mixB.insert(mixB.end(), L.begin(), L.end());
                for (int s = 0; s < tg::kStemSlots; s++) {
                    const float* p = g.stemOutL(s);
                    if (p) parts[s].insert(parts[s].end(), p, p + n);
                    else   parts[s].insert(parts[s].end(), (size_t)n, 0.f);
                }
            }
        }

        double worst = 0, energy = 0;
        for (size_t i = 0; i < mixA.size(); i++) {
            worst = std::max(worst, (double)std::fabs(mixA[i] - mixB[i]));
            energy = std::max(energy, (double)std::fabs(mixA[i]));
        }
        snprintf(msg, sizeof msg, "focus %-4s  peak %.3f, largest difference %.2e",
                 fname[focus], energy, worst);
        ok("the main output is unchanged by splitting", energy > 0.001 && worst < 1e-5, msg);

        // and the four parts must add back up to it
        double sworst = 0;
        for (size_t i = 0; i < mixB.size(); i++) {
            double t = 0;
            for (int s = 0; s < tg::kStemSlots; s++) t += parts[s][i];
            sworst = std::max(sworst, std::fabs(t - mixB[i]));
        }
        snprintf(msg, sizeof msg, "focus %-4s  largest difference %.2e", fname[focus], sworst);
        ok("the four separate outputs sum back to the main", sworst < 1e-5, msg);
    }

    // each output must actually carry its own stem, not a copy of the mix
    {
        tg::TapeGrain g; setup(g, true, tg::fFull);
        const int n = 512;
        std::vector<float> L(n), R(n);
        std::vector<std::vector<float>> parts(tg::kStemSlots);
        for (int b = 0; b < 40; b++) {
            g.process(nullptr, nullptr, L.data(), R.data(), n);
            for (int s = 0; s < tg::kStemSlots; s++) {
                const float* p = g.stemOutL(s);
                if (p) parts[s].insert(parts[s].end(), p, p + n);
            }
        }
        // correlate each output against the tone that went into that slot
        const double hz[4] = {110, 220, 440, 880};
        bool right = true;
        std::string detail;
        for (int s = 0; s < tg::kStemSlots; s++) {
            double best = 0; int bestAt = -1;
            for (int k = 0; k < tg::kStemSlots; k++) {
                double re = 0, im = 0;
                for (size_t i = 0; i < parts[s].size(); i++) {
                    const double t = 2*kPi*hz[k]*(double)i/SR;
                    re += parts[s][i]*std::cos(t); im -= parts[s][i]*std::sin(t);
                }
                const double m = std::hypot(re, im);
                if (m > best) { best = m; bestAt = k; }
            }
            char b2[32]; snprintf(b2, sizeof b2, "%d->%d ", s, bestAt);
            detail += b2;
            if (bestAt != s) right = false;
        }
        ok("each output carries its own stem", right, detail);

        // turning a stem down must silence its output, not just the mix
        g.setParam(tg::kStemC, 0.f);
        for (int b = 0; b < 20; b++) g.process(nullptr, nullptr, L.data(), R.data(), n);
        const float* p = g.stemOutL(2);
        float pk = 0; if (p) for (int i = 0; i < n; i++) pk = std::max(pk, std::fabs(p[i]));
        snprintf(msg, sizeof msg, "peak %.2e", pk);
        ok("a stem turned down goes silent on its own output", pk < 1e-6, msg);
    }

    // off by default, and nothing is handed out when it is off
    {
        tg::TapeGrain g; setup(g, false, tg::fFull);
        ok("split outputs are off unless asked for", !g.splitOutputs());
        ok("and no buffer is handed out when they are off", g.stemOutL(0) == nullptr);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
