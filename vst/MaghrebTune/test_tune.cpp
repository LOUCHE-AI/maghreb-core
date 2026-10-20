// Detection and snapping, measured.
//
// The claim this plug-in makes is narrow and checkable: it puts a sung note on
// a degree of the ṭabʿ, including the degrees twelve-tone cannot reach. So the
// tests synthesise notes at known frequencies and check where they land, in
// cents, against both grids.

#include "tune_detect.h"
#include "tune_scale.h"

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

// A voice is not a sine: the harmonics are what make naive autocorrelation
// drop an octave, so the test signal has to have them.
static std::vector<float> voiceLike(double hz, int n, double vibratoCents = 0.0,
                                    double vibratoHz = 5.0) {
    std::vector<float> x(n);
    double phase = 0.0;
    for (int i = 0; i < n; i++) {
        const double t = i / SR;
        const double f = hz * std::pow(2.0, vibratoCents *
                                       std::sin(2 * M_PI * vibratoHz * t) / 1200.0);
        phase += 2 * M_PI * f / SR;
        double s = 0.0;
        // a falling harmonic series, roughly like a sung vowel
        for (int h = 1; h <= 8; h++) s += std::sin(phase * h) / h;
        x[i] = (float)(0.25 * s);
    }
    return x;
}

static double centsBetween(double a, double b) {
    return (a > 0 && b > 0) ? 1200.0 * std::log2(a / b) : 1e9;
}

int main() {
    char msg[400];
    mtune::Detector det;
    det.setup(SR, 2048);
    det.setRange(70.0, 1000.0);

    // ---- detection accuracy across a singer's range ----
    {
        double worst = 0.0;
        double worstAt = 0.0;
        bool allVoiced = true;
        for (double hz = 90.0; hz <= 700.0; hz *= 1.06) {
            std::vector<float> x = voiceLike(hz, 4096);
            mtune::Pitch p = det.analyse(x.data() + 1024, 2048);
            if (!p.voiced) allVoiced = false;
            const double err = std::fabs(centsBetween(p.hz, hz));
            if (err > worst) { worst = err; worstAt = hz; }
        }
        snprintf(msg, sizeof msg, "worst %.2f cents, at %.0f Hz", worst, worstAt);
        // a neutral interval sits 50 cents off a semitone, so anything above
        // about 10 cents of error would start to blur which degree is meant
        ok("pitch is detected to within a few cents across the range",
           allVoiced && worst < 5.0, msg);
    }

    // ---- no octave errors on a harmonically rich note ----
    {
        int wrongOctave = 0;
        for (double hz = 100.0; hz <= 500.0; hz *= 1.03) {
            std::vector<float> x = voiceLike(hz, 4096);
            mtune::Pitch p = det.analyse(x.data() + 1024, 2048);
            const double err = centsBetween(p.hz, hz);
            if (std::fabs(err) > 600.0) wrongOctave++;
        }
        snprintf(msg, sizeof msg, "%d octave errors", wrongOctave);
        ok("and never drops an octave, which would move it to another degree",
           wrongOctave == 0, msg);
    }

    // ---- vibrato is followed, not smeared ----
    {
        std::vector<float> x = voiceLike(220.0, 8192, 60.0, 5.5);
        double lo = 1e9, hi = -1e9;
        for (int off = 0; off + 2048 < (int)x.size(); off += 256) {
            mtune::Pitch p = det.analyse(x.data() + off, 2048);
            if (!p.voiced) continue;
            const double c = centsBetween(p.hz, 220.0);
            lo = std::min(lo, c); hi = std::max(hi, c);
        }
        snprintf(msg, sizeof msg, "swing %.0f cents seen of 120 applied", hi - lo);
        ok("a 60 cent vibrato is followed", (hi - lo) > 60.0, msg);
    }

    // ---- the point: Sika's neutral third ----
    //
    // Sika is {0, 150, 350, 500, 700, 850, 1050} above the tonic. Its third at
    // 350 cents is 50 from E flat and 50 from E: exactly the note an ordinary
    // corrector has to guess about, and gets wrong either way.
    {
        tubu::Tuning t;
        t.setTab(3);                       // Sika
        const double tonic = 60.0;         // C
        const double tonicHz = mtune::hzForMidi(tonic);

        bool allRight = true;
        std::string bad;
        for (int d = 0; d < tubu::kDegrees; d++) {
            const double want = tonicHz * std::pow(2.0, t.degreeCents(d) / 1200.0);
            // sung four cents sharp of the degree, as a singer would
            const double sung = want * std::pow(2.0, 4.0 / 1200.0);
            mtune::Target tar = mtune::nearestDegree(t, sung, tonic);
            const double err = std::fabs(centsBetween(tar.hz, want));
            if (!tar.found || tar.degree != d || err > 0.01) {
                allRight = false;
                if (bad.empty()) bad = "degree " + std::to_string(d);
            }
        }
        ok("every degree of Sika is snapped to exactly", allRight, bad);

        // and what an ordinary corrector does to the same notes
        double worstTet = 0.0;
        int missed = 0;
        for (int d = 0; d < tubu::kDegrees; d++) {
            const double want = tonicHz * std::pow(2.0, t.degreeCents(d) / 1200.0);
            const double tet = mtune::nearestSemitoneHz(want);
            const double err = std::fabs(centsBetween(tet, want));
            if (err > 1.0) missed++;
            worstTet = std::max(worstTet, err);
        }
        snprintf(msg, sizeof msg, "%d of %d degrees moved, worst %.0f cents; "
                 "the tabʿ has %d neutral degrees",
                 missed, tubu::kDegrees, worstTet, t.neutralCount());
        ok("where twelve-tone correction moves them off the tabʿ",
           missed == t.neutralCount() && worstTet > 45.0, msg);
    }

    // ---- across all eighteen ṭubūʿ ----
    {
        tubu::Tuning t;
        int neutralTotal = 0, snappedRight = 0, tested = 0;
        for (int tab = 0; tab < tubu::kNumTubu; tab++) {
            t.setTab(tab);
            neutralTotal += t.neutralCount();
            for (int d = 0; d < tubu::kDegrees; d++) {
                const double tonicHz = mtune::hzForMidi(57.0);
                const double want = tonicHz * std::pow(2.0, t.degreeCents(d) / 1200.0);
                const double sung = want * std::pow(2.0, -7.0 / 1200.0);
                mtune::Target tar = mtune::nearestDegree(t, sung, 57.0);
                tested++;
                // Compared by frequency, not by degree number. Mazmum lists
                // 700 and 900 twice, so a note landing on the right pitch can
                // legitimately be reported under either index.
                if (tar.found && std::fabs(centsBetween(tar.hz, want)) < 0.01)
                    snappedRight++;
            }
        }
        snprintf(msg, sizeof msg, "%d/%d degrees, %d of them neutral",
                 snappedRight, tested, neutralTotal);
        ok("every degree of every tabʿ snaps to itself",
           snappedRight == tested && neutralTotal > 10, msg);
    }

    // Some ṭubūʿ list a degree twice - Mazmum has 700 and 900 in two slots
    // each - so a "degree" is not a unique name for a pitch. Worth pinning,
    // because a display that assumes seven distinct degrees will be wrong.
    {
        tubu::Tuning t;
        int dupes = 0;
        std::string which;
        for (int tab = 0; tab < tubu::kNumTubu; tab++) {
            t.setTab(tab);
            for (int a = 0; a < tubu::kDegrees; a++)
                for (int b = a + 1; b < tubu::kDegrees; b++)
                    if (t.degreeCents(a) == t.degreeCents(b)) {
                        dupes++;
                        if (which.empty()) which = tubu::kNames[tab];
                    }
        }
        snprintf(msg, sizeof msg, "%d repeated degrees, first in %s",
                 dupes, which.c_str());
        ok("some tubu repeat a degree, and that is the table, not a bug",
           dupes > 0, msg);
    }

    // ---- a note between two degrees goes to the nearer one ----
    {
        tubu::Tuning t;
        t.setTab(3);                        // Sika: 150 and 350 are neighbours
        const double tonicHz = mtune::hzForMidi(60.0);
        struct Case { double cents; int wantDegree; };
        const Case cases[] = {
            { 160.0, 1 }, { 200.0, 1 }, { 260.0, 2 }, { 340.0, 2 },
            {  10.0, 0 }, { 1190.0, 0 },      // just under the octave -> tonic above
        };
        bool allRight = true;
        std::string bad;
        for (const Case& c : cases) {
            const double hz = tonicHz * std::pow(2.0, c.cents / 1200.0);
            mtune::Target tar = mtune::nearestDegree(t, hz, 60.0);
            if (!tar.found || tar.degree != c.wantDegree) {
                allRight = false;
                if (bad.empty())
                    bad = std::to_string((int)c.cents) + " cents went to degree " +
                          std::to_string(tar.degree);
            }
        }
        ok("a note between degrees goes to the nearer one", allRight, bad);
    }

    // ---- the corrector's controls ----
    {
        mtune::Corrector c;
        c.setup(SR, 256);
        c.setAmount(1.0);
        c.setGlide(0.0);                    // hard snap, the raï setting
        c.setDeadzone(0.0);
        const double from = 200.0, to = 210.0;
        const double r = c.ratioFor(from, to);
        snprintf(msg, sizeof msg, "ratio %.5f, wanted %.5f", r, to / from);
        ok("at full amount and no glide it lands on the target at once",
           std::fabs(r - to / from) < 1e-6, msg);

        c.reset();
        c.setAmount(0.5);
        const double half = c.ratioFor(from, to);
        const double wantHalf = std::pow(2.0, 0.5 * 1200.0 * std::log2(to / from) / 1200.0);
        snprintf(msg, sizeof msg, "ratio %.5f, wanted %.5f", half, wantHalf);
        ok("half amount moves it half way, in cents", std::fabs(half - wantHalf) < 1e-6, msg);

        c.reset();
        c.setAmount(1.0);
        c.setDeadzone(20.0);
        const double inside = c.ratioFor(200.0, 200.0 * std::pow(2.0, 10.0 / 1200.0));
        ok("a lean smaller than the deadzone is left alone",
           std::fabs(inside - 1.0) < 1e-9);

        c.reset();
        c.setGlide(40.0);
        double last = 1.0;
        int steps = 0;
        for (; steps < 200; steps++) {
            last = c.ratioFor(from, to);
            if (std::fabs(1200.0 * std::log2(last) - 1200.0 * std::log2(to / from)) < 1.0) break;
        }
        snprintf(msg, sizeof msg, "%d hops (%.0f ms) to within a cent",
                 steps, steps * 256.0 / SR * 1000.0);
        ok("a glide takes time to arrive rather than jumping", steps > 3 && steps < 100, msg);
    }

    // ---- silence and noise must not produce a note ----
    {
        std::vector<float> quiet(4096, 0.f);
        mtune::Pitch p = det.analyse(quiet.data(), 2048);
        ok("silence is not a note", !p.voiced && p.hz == 0.0);

        std::vector<float> noise(4096);
        unsigned s = 22222;
        for (auto& v : noise) { s = s * 1664525u + 1013904223u; v = ((int)(s >> 9) - 4194304) / 4194304.f * 0.3f; }
        mtune::Pitch q = det.analyse(noise.data(), 2048);
        snprintf(msg, sizeof msg, "confidence %.2f, voiced %d", q.confidence, (int)q.voiced);
        ok("and noise is reported with low confidence", q.confidence < 0.9, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
