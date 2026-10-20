// The pitch shifter, measured.
//
// The claim is narrow: put a voice in, ask for a ratio, and the note that
// comes out is that ratio away in pitch, at the same level, with the vowel
// still recognisable. The first three are measurable directly; the fourth is
// approximated by the spectral centroid, which moves with the formants but
// should not move much with the pitch if the grains are doing their job.

#include "tune_shift.h"
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

// A vowel-ish signal: a harmonic series shaped by two fixed resonances, so
// there is a formant structure to preserve or destroy.
static std::vector<float> vowel(double hz, int n) {
    std::vector<float> x(n);
    double phase = 0.0;
    for (int i = 0; i < n; i++) {
        phase += 2 * M_PI * hz / SR;
        double s = 0.0;
        for (int h = 1; h <= 20; h++) {
            const double f = hz * h;
            if (f > SR * 0.45) break;
            // two formants, roughly an "ah"
            const double g1 = 1.0 / (1.0 + std::pow((f - 730.0) / 180.0, 2.0));
            const double g2 = 0.6 / (1.0 + std::pow((f - 1090.0) / 220.0, 2.0));
            s += (g1 + g2 + 0.08 / h) * std::sin(phase * h);
        }
        x[i] = (float)(0.2 * s);
    }
    return x;
}

static std::vector<float> run(const std::vector<float>& in, double ratio, double periodSamples) {
    mtune::Shifter sh;
    sh.setup(SR);
    sh.setRatio(ratio);
    sh.setPeriod(periodSamples);
    std::vector<float> out(in.size(), 0.f);
    const int block = 256;
    for (size_t i = 0; i + block <= in.size(); i += block)
        sh.process(in.data() + i, out.data() + i, block);
    return out;
}

static double rmsOf(const std::vector<float>& x, int from, int to) {
    double s = 0; int n = 0;
    for (int i = from; i < to && i < (int)x.size(); i++) { s += (double)x[i] * x[i]; n++; }
    return n ? std::sqrt(s / n) : 0.0;
}
// Where the first formant actually sits.
//
// A spectral centroid was the wrong tool: unwindowed, its leakage swamped the
// thing being measured and it moved 544 cents on a signal whose formants had
// not moved at all. This windows the block and finds the strongest peak in the
// region the first formant of an "ah" lives in, which is the number the claim
// is actually about.
static double formantHz(const std::vector<float>& x, int from, int len,
                        double loHz, double hiHz) {
    double best = 0.0, bestMag = -1.0;
    for (double f = loHz; f <= hiHz; f += 5.0) {
        double re = 0, im = 0;
        for (int i = 0; i < len && from + i < (int)x.size(); i++) {
            const double w = 0.5 * (1.0 - std::cos(2 * M_PI * i / (len - 1)));
            const double a = 2 * M_PI * f * i / SR;
            re += x[from + i] * w * std::cos(a);
            im -= x[from + i] * w * std::sin(a);
        }
        const double mag = re * re + im * im;
        if (mag > bestMag) { bestMag = mag; best = f; }
    }
    return best;
}

int main() {
    char msg[400];
    mtune::Detector det;
    det.setup(SR, 2048);
    det.setRange(70.0, 1200.0);

    const double hz = 200.0;
    const double period = SR / hz;
    std::vector<float> in = vowel(hz, 44100);

    // ---- the shift lands where it was asked to ----
    {
        double worst = 0.0; double worstAt = 1.0;
        bool allFinite = true;
        // A corrector moves a note to the nearest degree, so it asks for a
        // couple of hundred cents at most. Wider than that is measured
        // separately, because TD-PSOLA thins out when the grains stop
        // overlapping.
        for (double r : {0.794, 0.891, 0.944, 1.0, 1.059, 1.122, 1.26}) {
            std::vector<float> out = run(in, r, period);
            for (float v : out) if (!std::isfinite(v)) allFinite = false;
            // measure well past the priming latency
            mtune::Pitch p = det.analyse(out.data() + 20000, 2048);
            const double want = hz * r;
            const double err = p.hz > 0 ? std::fabs(1200.0 * std::log2(p.hz / want)) : 1e9;
            if (err > worst) { worst = err; worstAt = r; }
        }
        snprintf(msg, sizeof msg, "worst %.1f cents, at ratio %.3f", worst, worstAt);
        ok("every ratio lands within a few cents of where it was asked",
           allFinite && worst < 12.0, msg);
    }

    // ---- a semitone and a neutral second, the intervals this is for ----
    {
        bool good = true;
        std::string detail;
        for (double cents : {50.0, 100.0, 150.0, 350.0, -50.0, -150.0}) {
            const double r = std::pow(2.0, cents / 1200.0);
            std::vector<float> out = run(in, r, period);
            mtune::Pitch p = det.analyse(out.data() + 20000, 2048);
            const double got = p.hz > 0 ? 1200.0 * std::log2(p.hz / hz) : -9999;
            char b[64]; snprintf(b, sizeof b, "%+.0f->%+.0f ", cents, got);
            detail += b;
            if (std::fabs(got - cents) > 12.0) good = false;
        }
        ok("including the neutral intervals the tabʿ needs", good, detail);
    }

    // ---- unity is close to transparent ----
    {
        std::vector<float> out = run(in, 1.0, period);
        const double a = rmsOf(in, 20000, 24000);
        const double b = rmsOf(out, 20000, 24000);
        mtune::Pitch p = det.analyse(out.data() + 20000, 2048);
        const double err = std::fabs(1200.0 * std::log2(p.hz / hz));
        snprintf(msg, sizeof msg, "level %.3f vs %.3f, pitch off by %.1f cents", b, a, err);
        ok("at ratio one it passes the voice through", err < 3.0 && b > a * 0.7 && b < a * 1.4, msg);
    }

    // ---- level holds across ratios ----
    //
    // Shifting up packs the grains closer than the window was built for. The
    // window sum is divided out for exactly this reason, so the check is
    // whether that actually worked.
    {
        const double ref = rmsOf(run(in, 1.0, period), 20000, 24000);
        double worst = 0; double worstAt = 1;
        for (double r : {0.5, 0.75, 1.25, 1.5, 2.0}) {
            const double lvl = rmsOf(run(in, r, period), 20000, 24000);
            const double db = 20.0 * std::log10(std::max(1e-9, lvl / ref));
            if (std::fabs(db) > std::fabs(worst)) { worst = db; worstAt = r; }
        }
        snprintf(msg, sizeof msg, "worst %.1f dB, at ratio %.2f", worst, worstAt);
        ok("and the level holds within a few dB across ratios", std::fabs(worst) < 4.0, msg);
    }

    // ---- the vowel survives the shift ----
    //
    // Shift a voice with a plain resampler and the formants move with the
    // pitch; that is the chipmunk. PSOLA should leave them roughly put.
    {
        // The synthesised vowel puts its first formant at 730 Hz. Shift the
        // note up a fifth: if the grains carry the waveform shape, 730 stays
        // where it is. A plain resampler would drag it to about 1095.
        const double f0 = formantHz(run(in, 1.0, period), 20000, 4096, 400.0, 1400.0);
        const double fUp = formantHz(run(in, 1.5, period), 20000, 4096, 400.0, 1400.0);
        const double moved = 1200.0 * std::log2(std::max(1.0, fUp) / std::max(1.0, f0));
        // Asserted comparatively, because the measurement cannot do better.
        // The peak can only land on a harmonic, and near 730 Hz the harmonics
        // are 200 Hz apart at this f0 and 300 apart after the shift - four to
        // six hundred cents of quantisation. So "did not move at all" is not
        // measurable here; "moved far less than the pitch did" is, and it is
        // the claim that separates PSOLA from a resampler.
        const double shifted = 1200.0 * std::log2(1.5);
        snprintf(msg, sizeof msg,
                 "F1 %.0f -> %.0f Hz (%+.0f cents) while the pitch moved %+.0f; "
                 "a resampler would have dragged it to %.0f Hz",
                 f0, fUp, moved, shifted, f0 * 1.5);
        ok("the formants move far less than the pitch, so the vowel survives",
           std::fabs(moved) < shifted * 0.5, msg);
    }

    // ---- where it stops working, stated rather than hidden ----
    //
    // Shifting down spaces the grains further apart. At a ratio of 0.5 the hop
    // equals the grain length, the windows stop overlapping, and what comes out
    // is the original waveform repeated rather than a lower note. This finds
    // the edge instead of pretending there isn't one.
    {
        std::string detail;
        double firstBad = 0.0;
        for (double semis = -1; semis >= -12; semis -= 1) {
            const double r = std::pow(2.0, semis / 12.0);
            std::vector<float> out = run(in, r, period);
            mtune::Pitch p = det.analyse(out.data() + 20000, 2048);
            const double want = 100.0 * semis;
            const double got = p.hz > 0 ? 1200.0 * std::log2(p.hz / hz) : -9999;
            if (std::fabs(got - want) > 25.0 && firstBad == 0.0) firstBad = semis;
        }
        snprintf(msg, sizeof msg, "holds down to %.0f semitones",
                 firstBad == 0.0 ? -12.0 : firstBad + 1);
        ok("downward shifts hold well past anything a corrector asks for",
           firstBad == 0.0 || firstBad <= -5.0, msg);
    }

    // ---- silence in, silence out ----
    {
        std::vector<float> quiet(8192, 0.f);
        std::vector<float> out = run(quiet, 1.25, period);
        double pk = 0; for (float v : out) pk = std::max(pk, (double)std::fabs(v));
        snprintf(msg, sizeof msg, "peak %.2e", pk);
        ok("silence stays silent", pk < 1e-6, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
