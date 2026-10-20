// Does six-operator FM actually land on the tabʿ?
//
// The whole claim of this plug-in is that a DX7 patch can sit exactly on a
// neutral degree with no pitch bend anywhere. Asserting that the tuning table
// holds the right numbers proves nothing about what comes out, so this
// renders audio and measures its pitch in cents.

#include "tubudx_dsp.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static const int SR = 48000;

static std::vector<float> render(tdx::TubuDX& g, double seconds) {
    const int n = 256;
    const int blocks = (int)(seconds * SR / n);
    std::vector<float> out;
    std::vector<float> l(n), r(n);
    for (int b = 0; b < blocks; b++) {
        g.process(l.data(), r.data(), n);
        for (int i = 0; i < n; i++) out.push_back(l[i] + r[i]);
    }
    return out;
}

static double peakOf(const std::vector<float>& x) {
    double p = 0;
    for (size_t i = 0; i < x.size(); i++) p = std::max(p, (double)std::fabs(x[i]));
    return p;
}

// Energy at a frequency, via Goertzel - the same method used for the additive
// build, so the two are measured the same way.
static double energyAt(const std::vector<float>& x, double freq) {
    const double w = 2.0 * 3.14159265358979 * freq / SR;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < x.size(); i++) {
        const double s0 = x[i] + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// how far the strongest response sits from where it was asked for
static double centsOff(const std::vector<float>& x, double expect) {
    double best = expect, bestE = -1;
    for (int c = -80; c <= 80; c++) {
        const double f = expect * std::pow(2.0, c / 1200.0);
        const double e = energyAt(x, f);
        if (e > bestE) { bestE = e; best = f; }
    }
    return 1200.0 * std::log2(best / expect);
}

int main() {
    char msg[400];

    // ---- silence, then a note ----
    {
        tdx::TubuDX g;
        g.setSampleRate(SR);
        std::vector<float> quiet = render(g, 0.3);
        snprintf(msg, sizeof msg, "peak %.9f", peakOf(quiet));
        ok("silent with nothing held", peakOf(quiet) < 1e-9, msg);

        g.noteOn(60, 1.f);
        std::vector<float> on = render(g, 0.5);
        snprintf(msg, sizeof msg, "peak %.4f", peakOf(on));
        ok("a held note sounds on the built-in patch", peakOf(on) > 1e-3, msg);

        g.noteOff(60);
        render(g, 2.0);
        std::vector<float> after = render(g, 0.5);
        snprintf(msg, sizeof msg, "peak %.7f, %d voices", peakOf(after), g.activeVoices());
        ok("note off releases it", peakOf(after) < 1e-4, msg);
    }

    // ---- the tonic is the tonic ----
    {
        tdx::TubuDX g;
        g.setSampleRate(SR);
        g.setParam(tdx::kTonic, 60.f);
        g.noteOn(60, 1.f);
        render(g, 0.05);
        std::vector<float> x = render(g, 0.6);
        const double want = 440.0 * std::pow(2.0, (60 - 69) / 12.0);
        const double off = centsOff(x, want);
        snprintf(msg, sizeof msg, "%.2f cents from %.3f Hz", off, want);
        ok("the tonic key sounds the tonic", std::fabs(off) < 3.0, msg);
    }

    // ---- every degree of Sika, in cents ----
    {
        const int sika[tubu::kDegrees] = {0, 150, 350, 500, 700, 850, 1050};
        double worst = 0;
        int worstAt = -1;
        for (int d = 0; d < tubu::kDegrees; d++) {
            tdx::TubuDX g;
            g.setSampleRate(SR);
            g.setParam(tdx::kTab, 3.f);                          // Sika
            g.setParam(tdx::kTonic, 60.f);
            g.setParam(tdx::kMapMode, (float)tubu::mapPerStep);
            g.noteOn(60 + d, 1.f);
            render(g, 0.05);
            std::vector<float> x = render(g, 0.6);
            const double want = 440.0 * std::pow(2.0,
                (60 + sika[d] / 100.0 - 69.0) / 12.0);
            const double off = std::fabs(centsOff(x, want));
            if (off > worst) { worst = off; worstAt = d; }
        }
        snprintf(msg, sizeof msg, "worst %.2f cents, at degree %d", worst, worstAt + 1);
        ok("every degree of Sika sounds within a few cents of its table",
           worst < 4.0, msg);
    }

    // ---- the interval 12-TET has no key for ----
    {
        tdx::TubuDX g;
        g.setSampleRate(SR);
        g.setParam(tdx::kTab, 3.f);
        g.setParam(tdx::kTonic, 60.f);
        g.setParam(tdx::kMapMode, (float)tubu::mapPerStep);
        g.noteOn(61, 1.f);                     // Sika's second, 150 cents
        render(g, 0.05);
        std::vector<float> x = render(g, 0.6);

        const double cs = 440.0 * std::pow(2.0, (61 - 69) / 12.0);
        const double d  = 440.0 * std::pow(2.0, (62 - 69) / 12.0);
        snprintf(msg, sizeof msg, "%.0f cents from C#, %.0f cents from D",
                 centsOff(x, cs), centsOff(x, d));
        ok("the second sits midway between the two keys either side",
           std::fabs(centsOff(x, cs) - 50.0) < 5.0 &&
           std::fabs(centsOff(x, d) + 50.0) < 5.0, msg);
    }

    // ---- two neutral degrees at once, which bend cannot do ----
    {
        tdx::TubuDX g;
        g.setSampleRate(SR);
        g.setParam(tdx::kTab, 3.f);
        g.setParam(tdx::kTonic, 60.f);
        g.setParam(tdx::kMapMode, (float)tubu::mapPerStep);
        g.noteOn(61, 1.f);                     // 150 cents
        g.noteOn(62, 1.f);                     // 350 cents
        render(g, 0.05);
        std::vector<float> x = render(g, 0.6);

        const double f150 = 440.0 * std::pow(2.0, (61.5 - 69) / 12.0);
        const double f350 = 440.0 * std::pow(2.0, (63.5 - 69) / 12.0);
        const double e150 = energyAt(x, f150), e350 = energyAt(x, f350);
        const double n200 = energyAt(x, 440.0 * std::pow(2.0, (62 - 69) / 12.0));
        const double n300 = energyAt(x, 440.0 * std::pow(2.0, (63 - 69) / 12.0));
        const double floorE = std::max(n200, n300);
        snprintf(msg, sizeof msg, "%.0fx the energy at the nearest tempered keys",
                 floorE > 0 ? std::min(e150, e350) / floorE : 0.0);
        ok("both neutral degrees sound together, each at its own pitch",
           e150 > floorE * 10 && e350 > floorE * 10, msg);
        snprintf(msg, sizeof msg, "%d voices", g.activeVoices());
        ok("and they are two separate voices", g.activeVoices() >= 2, msg);
    }

    // ---- a different tabʿ really is different ----
    {
        auto secondOf = [&](int tab) {
            tdx::TubuDX g;
            g.setSampleRate(SR);
            g.setParam(tdx::kTab, (float)tab);
            g.setParam(tdx::kTonic, 60.f);
            g.setParam(tdx::kMapMode, (float)tubu::mapPerStep);
            g.noteOn(61, 1.f);
            render(g, 0.05);
            std::vector<float> x = render(g, 0.5);
            const double tonic = 440.0 * std::pow(2.0, (60 - 69) / 12.0);
            // find the strongest partial near the expected second
            double best = 0, bestE = -1;
            for (int c = 100; c <= 260; c++) {
                const double f = tonic * std::pow(2.0, c / 1200.0);
                const double e = energyAt(x, f);
                if (e > bestE) { bestE = e; best = c; }
            }
            return best;
        };
        const double sika = secondOf(3);    // 150
        const double dhil = secondOf(0);    // 200
        snprintf(msg, sizeof msg, "Sika %.0f cents, Dhil %.0f cents", sika, dhil);
        ok("Sika's neutral second and Dhil's whole tone are not the same note",
           std::fabs(sika - 150.0) < 8.0 && std::fabs(dhil - 200.0) < 8.0, msg);
    }

    // ---- editing a degree moves what sounds ----
    {
        tdx::TubuDX g;
        g.setSampleRate(SR);
        g.setParam(tdx::kTab, 3.f);
        g.setParam(tdx::kTonic, 60.f);
        g.setParam(tdx::kMapMode, (float)tubu::mapPerStep);
        g.setParam(tdx::kEditDegree, 1.f);
        g.setParam(tdx::kEditCents, 170.f);      // move the second up 20 cents
        g.noteOn(61, 1.f);
        render(g, 0.05);
        std::vector<float> x = render(g, 0.6);
        const double want = 440.0 * std::pow(2.0, (60 + 1.7 - 69) / 12.0);
        snprintf(msg, sizeof msg, "%.2f cents from 170", centsOff(x, want));
        ok("retuning a degree moves the note it plays",
           std::fabs(centsOff(x, want)) < 5.0, msg);
    }

    // ---- the drone follows the tabʿ ----
    {
        tdx::TubuDX g;
        g.setSampleRate(SR);
        g.setParam(tdx::kTab, 16.f);             // Mujannab: fifth is 650
        g.setParam(tdx::kTonic, 60.f);
        g.setParam(tdx::kDroneLevel, 1.f);
        g.setParam(tdx::kDrone, 2.f);            // tonic and fifth
        render(g, 0.05);
        std::vector<float> x = render(g, 0.6);
        const double f650 = 440.0 * std::pow(2.0, (66.5 - 69) / 12.0);
        const double f700 = 440.0 * std::pow(2.0, (67 - 69) / 12.0);
        const double e650 = energyAt(x, f650), e700 = energyAt(x, f700);
        snprintf(msg, sizeof msg, "650c has %.0fx the energy of 700c",
                 e700 > 0 ? e650 / e700 : 0.0);
        ok("the drone fifth is the tabʿ's, not a tempered one", e650 > e700 * 5, msg);

        g.setParam(tdx::kDrone, 0.f);
        render(g, 1.5);
        std::vector<float> off = render(g, 0.4);
        snprintf(msg, sizeof msg, "peak %.7f", peakOf(off));
        ok("turning the drone off stops it", peakOf(off) < 1e-4, msg);
    }

    // ---- nothing silly anywhere ----
    {
        bool sane = true;
        double worst = 0;
        for (int t = 0; t < tubu::kNumTubu; t += 3) {
            for (int m = 0; m < tubu::kNumMapModes; m++) {
                tdx::TubuDX g;
                g.setSampleRate(SR);
                g.setParam(tdx::kTab, (float)t);
                g.setParam(tdx::kMapMode, (float)m);
                for (int k = 24; k <= 96; k += 12) g.noteOn(k, 1.f);
                // Per channel, not the mono fold render() returns: a host
                // clips each channel on its own, and summing them measured a
                // ceiling twice as high as the one that matters.
                const int n = 256;
                std::vector<float> l(n), r(n);
                for (int b = 0; b < (int)(0.4 * SR / n); b++) {
                    g.process(l.data(), r.data(), n);
                    for (int i = 0; i < n; i++) {
                        if (!std::isfinite(l[i]) || !std::isfinite(r[i])) sane = false;
                        worst = std::max(worst, (double)std::fabs(l[i]));
                        worst = std::max(worst, (double)std::fabs(r[i]));
                    }
                }
            }
        }
        snprintf(msg, sizeof msg, "worst peak %.2f", worst);
        ok("a sweep through the tabʿ and the mappings stays finite", sane, msg);
        // Was `< 8.0`, which is not a ceiling - it is a shrug. A host clips
        // anything over 1.0, and that is what "it makes a lot of noise when I
        // play chords" turned out to be.
        ok("and never goes over what a host can take", worst <= 1.0, msg);
    }

    // ---- the ceiling itself ----
    {
        bool untouched = true;
        for (float v = -0.7f; v <= 0.7f; v += 0.01f)
            if (mdsp::softClip(v) != v) untouched = false;
        ok("normal playing passes through the ceiling unchanged", untouched);

        double worst = 0;
        for (float v = 0.f; v < 40.f; v += 0.05f)
            worst = std::max(worst, (double)std::fabs(mdsp::softClip(v)));
        snprintf(msg, sizeof msg, "worst %.4f from inputs up to 40", worst);
        ok("and nothing above it goes past full scale", worst <= 1.0, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
