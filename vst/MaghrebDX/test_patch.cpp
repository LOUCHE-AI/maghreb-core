// Do the derived DX7 patches behave like the strokes they came from?
//
// The point of the derivation is that the kit is recognisably the same kit,
// rendered by a different engine. So the checks are the same ones the 2-op
// version passes - every stroke sounds, a doum is low and a tak is high, the
// decays are in the right proportion - rather than a comparison of waveforms,
// which two different engines will never satisfy.

#include "dx_patch.h"

#include "msfa/synth.h"
#include "msfa/freqlut.h"
#include "msfa/sin.h"
#include "msfa/exp2.h"
#include "msfa/pitchenv.h"
#include "msfa/env.h"
#include "msfa/lfo.h"
#include "msfa/dx7note.h"
#include "msfa/controllers.h"
#include "msfa/fm_core.h"
#include "msfa/tuning.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static const double SR = 44100.0;
static FmCore g_core;
static Controllers g_ctrls;
static std::shared_ptr<TuningState> g_tuning;

static void initEngine() {
    Freqlut::init(SR); Sin::init(); Exp2::init(); Tanh::init();
    PitchEnv::init(SR); Env::init_sr(SR); Lfo::init(SR);
    g_tuning = createStandardTuning();
    g_ctrls.core = &g_core;
    for (int i = 0; i < 132; i++) g_ctrls.values_[i] = 0;
    g_ctrls.values_[kControllerPitch] = 0x2000;
    g_ctrls.modwheel_cc = g_ctrls.foot_cc = 0;
    g_ctrls.breath_cc = g_ctrls.aftertouch_cc = 0;
    strcpy(g_ctrls.opSwitch, "111111");
    g_ctrls.refresh();
}

static std::vector<float> render(const uint8_t* patch, int note, double seconds) {
    Dx7Note n(g_tuning, nullptr);
    n.init(patch, note, 99, 1, &g_ctrls);
    const int blocks = (int)(seconds * SR / N);
    std::vector<float> out;
    out.reserve(blocks * N);
    for (int b = 0; b < blocks; b++) {
        int32_t buf[N] = {0};
        n.compute(buf, 0, 0, &g_ctrls);
        for (int i = 0; i < N; i++) out.push_back((float)(buf[i] / (double)(1 << 24)));
    }
    return out;
}

static double peakOf(const std::vector<float>& x) {
    double p = 0;
    for (size_t i = 0; i < x.size(); i++) p = std::max(p, (double)std::fabs(x[i]));
    return p;
}
// crossing rate over the sounding part only - a fixed window would just be
// measuring decay length
static double pitchOf(const std::vector<float>& x) {
    const double pk = peakOf(x);
    if (pk <= 0) return 0;
    size_t end = 0;
    for (size_t i = 0; i < x.size(); i++) if (std::fabs(x[i]) > pk * 0.05) end = i;
    if (end < 64) return 0;
    int c = 0;
    for (size_t i = 1; i < end; i++) if ((x[i] < 0) != (x[i - 1] < 0)) c++;
    return (double)c / end * SR * 0.5;
}
static double decayOf(const std::vector<float>& x) {
    const double pk = peakOf(x);
    if (pk <= 0) return 0;
    const int chunk = 32;
    for (size_t i = 0; i + chunk <= x.size(); i += chunk) {
        double m = 0;
        for (int k = 0; k < chunk; k++) m = std::max(m, (double)std::fabs(x[i + k]));
        if (i > (size_t)chunk && m < pk * 0.01) return (double)i / SR;
    }
    return (double)x.size() / SR;
}
// pitch measured over just the first few milliseconds, against the tail
static double pitchOfHead(const std::vector<float>& x, double seconds) {
    const size_t n = std::min(x.size(), (size_t)(seconds * SR));
    std::vector<float> head(x.begin(), x.begin() + n);
    return pitchOf(head);
}

int main() {
    char msg[400];
    initEngine();
    mdx::DeriveSettings s;

    // ---- the mappings invert correctly ----
    {
        bool okRate = true;
        std::string bad;
        const double want[] = {0.01, 0.05, 0.1, 0.3, 1.0, 2.0, 4.0};
        for (double w : want) {
            const int r = mdx::rateForSeconds(w);
            const double got = mdx::kRateSeconds[r];
            // the table is coarse, so within a factor of two is the goal
            if (got < w * 0.45 || got > w * 2.2) {
                okRate = false;
                if (bad.empty()) {
                    char b[80];
                    snprintf(b, sizeof b, "%.3fs asked, rate %d gives %.3fs", w, r, got);
                    bad = b;
                }
            }
        }
        ok("a decay time maps to a rate that is close to it", okRate, bad);

        ok("no pitch lift maps to the centre level",
           mdx::pitchEgLevelForSemis(0.0) == 50,
           std::to_string(mdx::pitchEgLevelForSemis(0.0)));
        ok("an octave of lift maps above centre",
           mdx::pitchEgLevelForSemis(12.0) > 60,
           std::to_string(mdx::pitchEgLevelForSemis(12.0)));

        int c, f;
        mdx::fixedFreqBytes(320.0, c, f);
        const double back = std::pow(10.0, (c * 100 + f) / 100.0);
        snprintf(msg, sizeof msg, "320 Hz -> coarse %d fine %d -> %.1f Hz", c, f, back);
        ok("a fixed frequency round trips", std::fabs(back - 320.0) < 320.0 * 0.03, msg);

        mdx::ratioBytes(5.38, c, f);
        const double r = (c == 0 ? 0.5 : c) * (1.0 + f / 100.0);
        snprintf(msg, sizeof msg, "5.38 -> coarse %d fine %d -> %.2f", c, f, r);
        ok("a ratio round trips", std::fabs(r - 5.38) < 0.1, msg);
    }

    // ---- every derived stroke sounds ----
    {
        bool allSound = true, allFinite = true;
        double lo = 1e9, hi = 0;
        std::string quiet;
        for (int i = 0; i < mseq::kNumStrokes; i++) {
            uint8_t p[156];
            mdx::derivePatch(mseq::kStrokes[i], i, s, p);
            std::vector<float> x = render(p, mdx::kFirstNote + i, 1.5);
            const double pk = peakOf(x);
            lo = std::min(lo, pk); hi = std::max(hi, pk);
            if (pk < 1e-3) { allSound = false; if (quiet.empty()) quiet = mseq::kStrokeNames[i]; }
            for (size_t k = 0; k < x.size(); k++) if (!std::isfinite(x[k])) allFinite = false;
        }
        snprintf(msg, sizeof msg, "peaks %.3f .. %.3f", lo, hi);
        ok("every one of the eleven derived strokes sounds", allSound,
           quiet.empty() ? msg : quiet);
        ok("and none of them produces NaN or inf", allFinite);
    }

    // ---- the kit is still recognisably the kit ----
    {
        auto pitchOfStroke = [&](int i) {
            uint8_t p[156];
            mdx::derivePatch(mseq::kStrokes[i], i, s, p);
            return pitchOf(render(p, mdx::kFirstNote + i, 0.6));
        };
        const double doum = pitchOfStroke(0);   // D
        const double tak  = pitchOfStroke(3);   // T
        const double ka   = pitchOfStroke(5);   // k
        snprintf(msg, sizeof msg, "doum %.0f, tak %.0f, ka %.0f Hz", doum, tak, ka);
        ok("a doum is low and a ka is high, as in the original",
           doum < tak && tak < ka, msg);

        // and the proportions are near the source engine's own figures
        snprintf(msg, sizeof msg, "tak is %.1fx the doum", tak / std::max(1.0, doum));
        ok("the spread between them is wide, not a few percent",
           tak > doum * 3.0, msg);
    }

    // ---- decay follows the stroke table ----
    {
        auto decayOfStroke = [&](int i) {
            uint8_t p[156];
            mdx::derivePatch(mseq::kStrokes[i], i, s, p);
            return decayOf(render(p, mdx::kFirstNote + i, 3.0));
        };
        const double doum = decayOfStroke(0);   // ampDecay 0.42
        const double mute = decayOfStroke(2);   // ampDecay 0.085
        const double ka   = decayOfStroke(5);   // ampDecay 0.050
        snprintf(msg, sizeof msg, "doum %.2fs, mute %.2fs, ka %.2fs", doum, mute, ka);
        ok("the long stroke rings longer than the short ones",
           doum > mute && mute >= ka * 0.8, msg);
    }

    // ---- the pitch envelope really bends ----
    {
        // The modulator has its own decay, so a strike is always brighter at
        // the front whether or not the pitch moves - which is why the first
        // version of this test passed for the wrong reason. Silencing the
        // modulator and the noise leaves the pitch envelope as the only thing
        // that can shift the carrier.
        mdx::DeriveSettings bare = s;
        bare.fmAmount = 0.f;
        bare.noiseAmount = 0.f;

        uint8_t p[156];
        mdx::derivePatch(mseq::kStrokes[0], 0, bare, p);   // doum, bend 1.10
        std::vector<float> x = render(p, mdx::kFirstNote, 1.0);
        const double head = pitchOfHead(x, 0.02);
        const double whole = pitchOf(x);
        snprintf(msg, sizeof msg, "%.0f Hz in the first 20 ms, %.0f Hz over the whole strike",
                 head, whole);
        ok("the carrier starts higher than it ends", head > whole * 1.15, msg);

        // a stroke with no bend should not
        mseq::StrokeDef flat = mseq::kStrokes[0];
        flat.bend = 0.f;
        mdx::derivePatch(flat, 0, bare, p);
        std::vector<float> y = render(p, mdx::kFirstNote, 1.0);
        const double h2 = pitchOfHead(y, 0.02), w2 = pitchOf(y);
        snprintf(msg, sizeof msg, "%.0f Hz then %.0f Hz", h2, w2);
        ok("with the bend removed it holds its pitch", h2 < w2 * 1.15, msg);
    }

    // ---- the controls reach the sound ----
    {
        uint8_t p[156];
        // The carrier alone, for the same reason as the bend test: a crossing
        // rate over modulator and noise together is a blend, and shifting the
        // whole kit up an octave does not move that blend by a clean factor.
        mdx::DeriveSettings a = s;
        a.fmAmount = 0.f;
        a.noiseAmount = 0.f;
        mdx::derivePatch(mseq::kStrokes[0], 0, a, p);
        const double base = pitchOf(render(p, mdx::kFirstNote, 0.8));

        a.tune = 12.f;
        mdx::derivePatch(mseq::kStrokes[0], 0, a, p);
        const double up = pitchOf(render(p, mdx::kFirstNote, 0.8));
        snprintf(msg, sizeof msg, "%.0f Hz -> %.0f Hz", base, up);
        ok("Tune an octave up roughly doubles the carrier",
           up > base * 1.7 && up < base * 2.3, msg);

        a = s;

        a = s; a.decay = 3.f;
        mdx::derivePatch(mseq::kStrokes[0], 0, a, p);
        const double longer = decayOf(render(p, mdx::kFirstNote, 6.0));
        a.decay = 0.3f;
        mdx::derivePatch(mseq::kStrokes[0], 0, a, p);
        const double shorter = decayOf(render(p, mdx::kFirstNote, 6.0));
        snprintf(msg, sizeof msg, "%.2fs at 3x, %.2fs at 0.3x", longer, shorter);
        ok("Decay lengthens and shortens the tail", longer > shorter * 2.0, msg);

        // the modulator has to do something
        a = s; a.fmAmount = 0.f;
        mdx::derivePatch(mseq::kStrokes[3], 3, a, p);       // tak, index 1.3
        const double dull = pitchOf(render(p, mdx::kFirstNote + 3, 0.5));
        a.fmAmount = 2.f;
        mdx::derivePatch(mseq::kStrokes[3], 3, a, p);
        const double bright = pitchOf(render(p, mdx::kFirstNote + 3, 0.5));
        snprintf(msg, sizeof msg, "%.0f Hz with FM off, %.0f Hz with it doubled",
                 dull, bright);
        ok("opening the FM adds harmonics", bright > dull * 1.2, msg);

        // and so does the noise layer
        a = s; a.noiseAmount = 0.f;
        mdx::derivePatch(mseq::kStrokes[7], 7, a, p);       // slap, noise 0.90
        const double clean = pitchOf(render(p, mdx::kFirstNote + 7, 0.5));
        a.noiseAmount = 2.f;
        mdx::derivePatch(mseq::kStrokes[7], 7, a, p);
        const double noisy = pitchOf(render(p, mdx::kFirstNote + 7, 0.5));
        snprintf(msg, sizeof msg, "%.0f Hz clean, %.0f Hz noisy", clean, noisy);
        ok("the noise layer brightens the slap", noisy > clean * 1.2, msg);
    }

    // ---- the three kits stay apart ----
    {
        double c[mseq::kNumKits];
        for (int k = 0; k < mseq::kNumKits; k++) {
            mdx::DeriveSettings a = s;
            a.kitFreq = mseq::kKits[k].freq;
            a.kitDecay = mseq::kKits[k].decay;
            a.kitNoise = mseq::kKits[k].noise;
            a.kitIndex = mseq::kKits[k].index;
            uint8_t p[156];
            mdx::derivePatch(mseq::kStrokes[0], 0, a, p);
            c[k] = pitchOf(render(p, mdx::kFirstNote, 0.6));
        }
        snprintf(msg, sizeof msg, "bendir %.0f, darbuka %.0f, tar %.0f Hz",
                 c[0], c[1], c[2]);
        ok("the kits are still pitched apart, tar highest",
           c[0] < c[1] && c[1] < c[2], msg);
    }

    // ---- nothing silly at the extremes ----
    {
        bool sane = true;
        double worst = 0;
        std::string bad;
        for (int i = 0; i < mseq::kNumStrokes; i++) {
            mdx::DeriveSettings a = s;
            a.tune = 24.f; a.fmAmount = 2.f; a.noiseAmount = 2.f;
            a.decay = 4.f; a.tone = 1.f;
            uint8_t p[156];
            mdx::derivePatch(mseq::kStrokes[i], i, a, p);
            std::vector<float> x = render(p, mdx::kFirstNote + i, 1.0);
            worst = std::max(worst, peakOf(x));
            for (size_t k = 0; k < x.size(); k++)
                if (!std::isfinite(x[k])) { sane = false; bad = mseq::kStrokeNames[i]; }
        }
        snprintf(msg, sizeof msg, "worst peak %.2f", worst);
        ok("every stroke stays finite with everything pushed", sane, bad.empty() ? msg : bad);
        ok("and bounded", worst < 8.0, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
