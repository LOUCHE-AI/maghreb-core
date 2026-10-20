// The presets, measured.
//
// A preset that merely loads without complaint is not worth having. These
// check that each one does what its name says: that the hard ones really do
// snap instantly, that the gentle ones really do leave a lean alone, and above
// all that Rai Hard and Twelve-Tone put the same sung note in two different
// places - which is the entire argument for the plug-in.

#include "tune_presets.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static const double SR = 44100.0;

static std::vector<float> sung(double hz, int n) {
    std::vector<float> x(n);
    double phase = 0.0;
    for (int i = 0; i < n; i++) {
        phase += 2 * M_PI * hz / SR;
        double s = 0.0;
        for (int h = 1; h <= 12; h++) {
            const double f = hz * h;
            if (f > SR * 0.45) break;
            const double g = 1.0 / (1.0 + std::pow((f - 730.0) / 220.0, 2.0));
            s += (g + 0.10 / h) * std::sin(phase * h);
        }
        x[i] = (float)(0.22 * s);
    }
    return x;
}

// A matched filter, for the reason the effect tests use one: YIN is the right
// detector for a voice and the wrong ruler for PSOLA output.
static double pitchAt(const std::vector<float>& x, int at, double near) {
    const int len = 4096;
    double best = near, bestMag = -1.0;
    for (double f = near / 1.35; f <= near * 1.35; f += 0.25) {
        double re = 0, im = 0;
        for (int i = 0; i < len && at + i < (int)x.size(); i++) {
            const double w = 0.5 * (1.0 - std::cos(2 * M_PI * i / (len - 1)));
            const double a = 2 * M_PI * f * i / SR;
            re += x[at + i] * w * std::cos(a);
            im -= x[at + i] * w * std::sin(a);
        }
        const double mag = re * re + im * im;
        if (mag > bestMag) { bestMag = mag; best = f; }
    }
    return best;
}
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

static void load(mtune::MaghrebTune& fx, const mtune::Preset& p) {
    fx.setParam(mtune::kTab, (float)p.tab);
    fx.setParam(mtune::kTonic, (float)p.tonic);
    fx.setParam(mtune::kTonicFine, 0.f);
    fx.setParam(mtune::kAmount, p.amount);
    fx.setParam(mtune::kRetune, p.retuneMs);
    fx.setParam(mtune::kDeadzone, p.deadCents);
    fx.setParam(mtune::kMix, p.mix);
    fx.setParam(mtune::kOutput, 0.f);
    fx.setParam(mtune::kBypass, 0.f);
}
static std::vector<float> run(mtune::MaghrebTune& fx, const std::vector<float>& in) {
    std::vector<float> out(in.size(), 0.f);
    const int B = 256;
    for (size_t i = 0; i + B <= in.size(); i += B)
        fx.process(in.data() + i, out.data() + i, B);
    return out;
}

int main() {
    char msg[500];

    // ---- every preset is in range and names a real tabʿ ----
    {
        bool sane = true;
        std::string bad;
        for (int i = 0; i < mtune::kNumPresets; i++) {
            const mtune::Preset& p = mtune::kPresets[i];
            const bool okp = p.tab >= 0 && p.tab < tubu::kNumTubu &&
                             p.tonic >= 36 && p.tonic <= 84 &&
                             p.amount >= 0.f && p.amount <= 1.f &&
                             p.retuneMs >= 0.f && p.retuneMs <= 200.f &&
                             p.deadCents >= 0.f && p.deadCents <= 50.f &&
                             p.mix >= 0.f && p.mix <= 1.f &&
                             p.name && p.note;
            if (!okp) { sane = false; if (bad.empty()) bad = p.name; }
        }
        snprintf(msg, sizeof msg, "%d presets", mtune::kNumPresets);
        ok("every preset is inside the parameter ranges", sane, bad.empty() ? msg : bad);
    }

    // ---- the argument: the same note, two grids ----
    //
    // Sing Sika's neutral third. Rai Hard should hold it there; Twelve-Tone
    // should drag it to the nearest semitone, which is fifty cents away.
    {
        tubu::Tuning sika; sika.setTab(3);
        const double tonicHz = mtune::hzForMidi(60.0);
        const double neutral = tonicHz * std::pow(2.0, 350.0 / 1200.0);
        std::vector<float> in = sung(neutral, 40000);

        mtune::MaghrebTune a; a.setSampleRate(SR); load(a, mtune::kPresets[0]);   // Rai Hard
        mtune::MaghrebTune b; b.setSampleRate(SR); load(b, mtune::kPresets[9]);   // Twelve-Tone

        const double rai = pitchAt(run(a, in), 22000, neutral);
        const double tet = pitchAt(run(b, in), 22000, neutral);

        snprintf(msg, sizeof msg,
                 "sang 350 c; Rai Hard kept it at %+.0f c, Twelve-Tone moved it to %+.0f c",
                 cents(rai, tonicHz) - 0.0 - 0.0 + 0.0 - cents(neutral, tonicHz) + 350.0,
                 cents(tet, tonicHz));
        ok("Rai Hard holds the neutral third where Twelve-Tone cannot",
           std::fabs(cents(rai, neutral)) < 12.0 &&
           std::fabs(cents(tet, neutral)) > 30.0, msg);
    }

    // ---- hard presets snap, gentle ones do not ----
    {
        tubu::Tuning t;
        bool right = true;
        std::string detail;
        for (int i = 0; i < mtune::kNumPresets; i++) {
            const mtune::Preset& p = mtune::kPresets[i];
            t.setTab(p.tab);
            const double tonicHz = mtune::hzForMidi(p.tonic);
            // a note thirty cents under the tabʿ's third
            const double want = tonicHz * std::pow(2.0, t.degreeCents(2) / 1200.0);
            const double flat = want * std::pow(2.0, -30.0 / 1200.0);

            mtune::MaghrebTune fx; fx.setSampleRate(SR); load(fx, p);
            // Measured fully wet, whatever the preset's own blend is. This
            // assertion is about how far the corrector moves a note, and a
            // blend puts the uncorrected note back in beside the corrected
            // one - two pitches thirty cents apart, from which pitchAt returns
            // whichever partial happens to be stronger in that window. Parallel
            // Hard (mix 0.5) wandered between -62 and +183 cents depending only
            // on where it was sampled, and passed or failed on luck.
            fx.setParam(mtune::kMix, 1.f);
            const double got = pitchAt(run(fx, sung(flat, 40000)), 22000, want);
            const double left = cents(got, want);          // 0 = fully corrected

            // what the settings say should happen to a thirty cent error
            const double expected = (p.deadCents >= 30.f) ? -30.0
                                                          : -30.0 * (1.0 - p.amount);
            char b[96];
            snprintf(b, sizeof b, "%s %+.0f ", p.name, left);
            detail += b;
            const double tol = 16.0;
            if (std::fabs(left - expected) > tol) right = false;
        }
        ok("each preset corrects by as much as its settings claim", right, detail);
    }

    // ---- Tuner Only really touches nothing ----
    {
        tubu::Tuning t; t.setTab(mtune::kPresets[8].tab);
        const double tonicHz = mtune::hzForMidi(mtune::kPresets[8].tonic);
        const double flat = tonicHz * std::pow(2.0, (t.degreeCents(2) - 40.0) / 1200.0);

        mtune::MaghrebTune fx; fx.setSampleRate(SR); load(fx, mtune::kPresets[8]);
        const double got = pitchAt(run(fx, sung(flat, 40000)), 22000, flat);
        snprintf(msg, sizeof msg, "left it %+.0f cents from where it was sung",
                 cents(got, flat));
        ok("Tuner Only leaves the pitch exactly where it was sung",
           std::fabs(cents(got, flat)) < 10.0, msg);
    }

    // ---- the ṭabʿ each preset loads is the one it advertises ----
    {
        bool right = true;
        std::string detail;
        for (int i = 0; i < mtune::kNumPresets; i++) {
            const mtune::Preset& p = mtune::kPresets[i];
            mtune::MaghrebTune fx;
            fx.setSampleRate(SR);
            load(fx, p);
            if (std::string(fx.tabName()) != std::string(tubu::kNames[p.tab])) right = false;
        }
        // and the two that are meant to differ really do
        tubu::Tuning a, b;
        a.setTab(mtune::kPresets[0].tab);
        b.setTab(mtune::kPresets[9].tab);
        snprintf(msg, sizeof msg, "Rai Hard on %s (%d neutral), Twelve-Tone on %s (%d)",
                 tubu::kNames[mtune::kPresets[0].tab], a.neutralCount(),
                 tubu::kNames[mtune::kPresets[9].tab], b.neutralCount());
        ok("each preset loads the tabʿ it names, and the A/B pair really differ",
           right && a.neutralCount() > 0 && b.neutralCount() == 0, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
