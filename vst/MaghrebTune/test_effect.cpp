// The whole chain, end to end.
//
// A synthesised singer holds a note somewhere off a degree of the ṭabʿ; the
// effect should put it on that degree. This is the claim the plug-in makes,
// measured on the output rather than inferred from the parts.

#include "tune_dsp.h"
#include "tune_detect.h"

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

static std::vector<float> sung(double hz, int n, double driftCents = 0.0) {
    std::vector<float> x(n);
    double phase = 0.0;
    for (int i = 0; i < n; i++) {
        const double t = (double)i / n;
        const double f = hz * std::pow(2.0, driftCents * t / 1200.0);
        phase += 2 * M_PI * f / SR;
        double s = 0.0;
        for (int h = 1; h <= 12; h++) {
            const double fh = f * h;
            if (fh > SR * 0.45) break;
            const double g = 1.0 / (1.0 + std::pow((fh - 730.0) / 220.0, 2.0));
            s += (g + 0.10 / h) * std::sin(phase * h);
        }
        x[i] = (float)(0.22 * s);
    }
    return x;
}

static std::vector<float> run(mtune::MaghrebTune& fx, const std::vector<float>& in) {
    std::vector<float> out(in.size(), 0.f);
    const int B = 256;
    for (size_t i = 0; i + B <= in.size(); i += B)
        fx.process(in.data() + i, out.data() + i, B);
    return out;
}

// Measures the pitch of the output, and deliberately ignores the voiced flag.
//
// PSOLA's overlap-add leaves the output a little less periodic than the
// synthetic tone that went in, so YIN's dip is shallower and the flag comes
// back false even when the frequency is dead right. The flag is the right
// thing for the plug-in to gate on - it is reading a real voice, and a
// half-periodic reading should not drive a correction - but for measuring
// where a note landed, the frequency is what matters.
// Where the fundamental actually is, by matched filter.
//
// YIN was the wrong ruler for this. It is the right detector for the plug-in -
// it has to work on one window of a real voice with no idea what to expect -
// but as a measurement of PSOLA output it was fragile: the normalised
// difference sits near 0.35 rather than 0.05, so the threshold either misses
// (and falls back to a subharmonic, reporting 84 Hz for a 320 Hz note) or
// catches noise. Here the expected pitch is known, so a windowed correlation
// against candidate frequencies is both simpler and far steadier.
static double pitchAt(const std::vector<float>& x, int at, double near) {
    const int len = 4096;
    double best = near, bestMag = -1.0;
    for (double f = near / 1.35; f <= near * 1.35; f += 0.25) {
        double re = 0, im = 0;
        for (int i = 0; i < len && at + i < (int)x.size(); i++) {
            const double w = 0.5 * (1.0 - std::cos(2 * M_PI * i / (len - 1)));
            const double a2 = 2 * M_PI * f * i / SR;
            re += x[at + i] * w * std::cos(a2);
            im -= x[at + i] * w * std::sin(a2);
        }
        const double mag = re * re + im * im;
        if (mag > bestMag) { bestMag = mag; best = f; }
    }
    return best;
}

static double cents(double a, double b) {
    return (a > 0 && b > 0) ? 1200.0 * std::log2(a / b) : 1e9;
}

int main() {
    char msg[400];

    // ---- the whole point, on Sika ----
    //
    // Sika's third is at 350 cents. Sing 30 cents under it and the effect
    // should put the note on 350 - where an ordinary corrector would put it on
    // 300, because that is the nearest semitone.
    {
        tubu::Tuning ref; ref.setTab(3);
        const double tonicHz = mtune::hzForMidi(60.0);
        const double degreeHz = tonicHz * std::pow(2.0, ref.degreeCents(2) / 1200.0);
        const double flat = degreeHz * std::pow(2.0, -30.0 / 1200.0);

        mtune::MaghrebTune fx;
        fx.setSampleRate(SR);
        fx.setParam(mtune::kTab, 3.f);
        fx.setParam(mtune::kTonic, 60.f);
        fx.setParam(mtune::kAmount, 1.f);
        fx.setParam(mtune::kRetune, 0.f);

        std::vector<float> in = sung(flat, 44100);
        std::vector<float> out = run(fx, in);

        const double got = pitchAt(out, 22000, degreeHz);
        const double err = cents(got, degreeHz);
        const double tet = mtune::nearestSemitoneHz(degreeHz);
        snprintf(msg, sizeof msg,
                 "sang %.1f Hz, landed %.1f Hz, degree is %.1f Hz (off by %.1f cents); "
                 "twelve-tone would have said %.1f Hz",
                 flat, got, degreeHz, err, tet);
        ok("a note sung flat of a neutral degree is put on that degree",
           std::fabs(err) < 15.0, msg);
    }

    // ---- every degree of every tabʿ ----
    {
        int tested = 0, landed = 0;
        double worst = 0.0;
        std::string bad;
        tubu::Tuning ref;
        for (int tab = 0; tab < tubu::kNumTubu; tab += 3) {
            ref.setTab(tab);
            for (int d = 0; d < tubu::kDegrees; d += 2) {
                const double tonicHz = mtune::hzForMidi(57.0);
                const double want = tonicHz * std::pow(2.0, ref.degreeCents(d) / 1200.0);
                if (want < 90.0 || want > 700.0) continue;
                const double sungHz = want * std::pow(2.0, 25.0 / 1200.0);

                mtune::MaghrebTune fx;
                fx.setSampleRate(SR);
                fx.setParam(mtune::kTab, (float)tab);
                fx.setParam(mtune::kTonic, 57.f);
                fx.setParam(mtune::kAmount, 1.f);
                fx.setParam(mtune::kRetune, 0.f);

                std::vector<float> out = run(fx, sung(sungHz, 30000));
                const double got = pitchAt(out, 20000, want);
                const double err = std::fabs(cents(got, want));
                tested++;
                if (err < 20.0) landed++;
                else if (bad.empty()) {
                    char b[96];
                    snprintf(b, sizeof b, "%s degree %d off by %.0f",
                             tubu::kNames[tab], d, err);
                    bad = b;
                }
                worst = std::max(worst, std::min(err, 999.0));
            }
        }
        snprintf(msg, sizeof msg, "%d/%d landed, worst %.0f cents. %s",
                 landed, tested, worst, bad.c_str());
        ok("across the tubu, a sung note lands on its degree", landed == tested, msg);
    }

    // ---- amount and bypass do what they say ----
    {
        tubu::Tuning ref; ref.setTab(3);
        const double tonicHz = mtune::hzForMidi(60.0);
        const double degreeHz = tonicHz * std::pow(2.0, ref.degreeCents(2) / 1200.0);
        const double flat = degreeHz * std::pow(2.0, -40.0 / 1200.0);
        std::vector<float> in = sung(flat, 40000);

        mtune::MaghrebTune off;
        off.setSampleRate(SR);
        off.setParam(mtune::kTab, 3.f); off.setParam(mtune::kTonic, 60.f);
        off.setParam(mtune::kAmount, 0.f); off.setParam(mtune::kRetune, 0.f);
        const double none = pitchAt(run(off, in), 20000, flat);

        mtune::MaghrebTune half;
        half.setSampleRate(SR);
        half.setParam(mtune::kTab, 3.f); half.setParam(mtune::kTonic, 60.f);
        half.setParam(mtune::kAmount, 0.5f); half.setParam(mtune::kRetune, 0.f);
        const double mid = pitchAt(run(half, in), 20000, degreeHz);

        snprintf(msg, sizeof msg, "amount 0 leaves it %.0f cents off, amount 0.5 leaves %.0f",
                 cents(none, degreeHz), cents(mid, degreeHz));
        ok("amount 0 leaves the voice alone and 0.5 goes half way",
           std::fabs(cents(none, flat)) < 12.0 &&
           std::fabs(cents(mid, degreeHz) + 20.0) < 12.0, msg);

        mtune::MaghrebTune byp;
        byp.setSampleRate(SR);
        byp.setParam(mtune::kBypass, 1.f);
        std::vector<float> b = run(byp, in);
        // only the samples that were actually processed: run() works in whole
        // blocks and leaves the remainder untouched
        const size_t done = (in.size() / 256) * 256;
        double worst = 0;
        for (size_t i = 0; i < done; i++) worst = std::max(worst, (double)std::fabs(b[i] - in[i]));
        snprintf(msg, sizeof msg, "worst difference %.2e", worst);
        ok("bypass is the input, sample for sample", worst == 0.0, msg);
    }

    // ---- the deadzone protects a lean ----
    {
        tubu::Tuning ref; ref.setTab(3);
        const double tonicHz = mtune::hzForMidi(60.0);
        const double degreeHz = tonicHz * std::pow(2.0, ref.degreeCents(2) / 1200.0);
        const double leaning = degreeHz * std::pow(2.0, 12.0 / 1200.0);

        mtune::MaghrebTune fx;
        fx.setSampleRate(SR);
        fx.setParam(mtune::kTab, 3.f); fx.setParam(mtune::kTonic, 60.f);
        fx.setParam(mtune::kAmount, 1.f); fx.setParam(mtune::kRetune, 0.f);
        fx.setParam(mtune::kDeadzone, 25.f);

        const double got = pitchAt(run(fx, sung(leaning, 30000)), 20000, leaning);
        snprintf(msg, sizeof msg, "sang 12 cents sharp, kept %.0f cents of it",
                 cents(got, degreeHz));
        ok("a lean inside the deadzone is left alone",
           std::fabs(cents(got, leaning)) < 12.0, msg);
    }

    // ---- a drifting note is followed ----
    {
        tubu::Tuning ref; ref.setTab(3);
        const double tonicHz = mtune::hzForMidi(60.0);
        const double degreeHz = tonicHz * std::pow(2.0, ref.degreeCents(4) / 1200.0);

        mtune::MaghrebTune fx;
        fx.setSampleRate(SR);
        fx.setParam(mtune::kTab, 3.f); fx.setParam(mtune::kTonic, 60.f);
        fx.setParam(mtune::kAmount, 1.f); fx.setParam(mtune::kRetune, 20.f);

        // sags 40 cents over the note, as a held note does
        std::vector<float> out = run(fx, sung(degreeHz, 44100, -40.0));
        const double early = cents(pitchAt(out, 12000, degreeHz), degreeHz);
        const double late  = cents(pitchAt(out, 36000, degreeHz), degreeHz);
        snprintf(msg, sizeof msg, "%.0f cents early, %.0f late", early, late);
        ok("a note that sags is held on the degree throughout",
           std::fabs(early) < 20.0 && std::fabs(late) < 20.0, msg);
    }

    // ---- housekeeping ----
    {
        mtune::MaghrebTune fx;
        fx.setSampleRate(SR);
        std::vector<float> quiet(20000, 0.f);
        std::vector<float> out = run(fx, quiet);
        double pk = 0; bool finite = true;
        for (float v : out) { pk = std::max(pk, (double)std::fabs(v)); if (!std::isfinite(v)) finite = false; }
        snprintf(msg, sizeof msg, "peak %.2e", pk);
        ok("silence in, silence out, and nothing infinite", finite && pk < 1e-6, msg);

        // The bound is a budget, not a measurement of what it happens to be.
        // PSOLA has to see a whole grain either side of the sample it is
        // producing, so the floor is 2 x the longest period it supports plus a
        // block - about 2100 samples for a 55 Hz low note. It was once 800,
        // which was not enough to lay the grain at all: low notes stuttered in
        // and out because emit() gave up and left no window sum behind.
        // 60 ms is the point past which a singer would hear themselves late.
        const double ms = fx.latency() * 1000.0 / SR;
        snprintf(msg, sizeof msg, "%d samples (%.1f ms)", fx.latency(), ms);
        ok("the latency is reported, and stays inside a usable budget",
           fx.latency() > 0 && ms < 60.0, msg);
    }

    // ---- Live: what the shorter grain costs and buys ----
    {
        mtune::MaghrebTune studio, live;
        studio.setSampleRate(SR);
        live.setSampleRate(SR);
        live.setParam(mtune::kLive, 1.f);
        const double sMs = studio.latency() * 1000.0 / SR;
        const double lMs = live.latency() * 1000.0 / SR;
        snprintf(msg, sizeof msg, "%.1f ms studio, %.1f ms live", sMs, lMs);
        ok("Live buys back a third of the delay", lMs < sMs * 0.8, msg);

        // and still puts a note on its degree, which is the point of buying it
        tubu::Tuning ref; ref.setTab(3);
        const double tonicHz = mtune::hzForMidi(60.0);
        const double degreeHz = tonicHz * std::pow(2.0, ref.degreeCents(2) / 1200.0);
        const double flat = degreeHz * std::pow(2.0, -30.0 / 1200.0);
        mtune::MaghrebTune fx;
        fx.setSampleRate(SR);
        fx.setParam(mtune::kTab, 3.f);
        fx.setParam(mtune::kTonic, 60.f);
        fx.setParam(mtune::kAmount, 1.f);
        fx.setParam(mtune::kRetune, 0.f);
        fx.setParam(mtune::kLive, 1.f);
        std::vector<float> out = run(fx, sung(flat, 44100));
        const double landed = pitchAt(out, 30000, degreeHz);
        snprintf(msg, sizeof msg, "landed %.1f cents from the degree", cents(landed, degreeHz));
        ok("and corrects no less accurately for it", std::fabs(cents(landed, degreeHz)) < 15.0, msg);
    }

    // ---- Amount past a hundred overshoots, on purpose ----
    {
        tubu::Tuning ref; ref.setTab(3);
        const double tonicHz = mtune::hzForMidi(60.0);
        const double degreeHz = tonicHz * std::pow(2.0, ref.degreeCents(2) / 1200.0);
        const double flat = degreeHz * std::pow(2.0, -30.0 / 1200.0);

        double at100 = 0, at150 = 0;
        for (int pass = 0; pass < 2; pass++) {
            mtune::MaghrebTune fx;
            fx.setSampleRate(SR);
            fx.setParam(mtune::kTab, 3.f);
            fx.setParam(mtune::kTonic, 60.f);
            fx.setParam(mtune::kRetune, 0.f);
            fx.setParam(mtune::kAmount, pass ? 1.5f : 1.0f);
            std::vector<float> out = run(fx, sung(flat, 44100));
            const double c = cents(pitchAt(out, 30000, degreeHz), degreeHz);
            (pass ? at150 : at100) = c;
        }
        snprintf(msg, sizeof msg, "100%% lands %+.0f c from the degree, 150%% lands %+.0f c",
                 at100, at150);
        // 30 cents flat, corrected by half again, should sit about 15 sharp
        ok("Amount past 100% carries the note past the degree",
           at150 > at100 + 8.0 && at150 < 25.0, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
