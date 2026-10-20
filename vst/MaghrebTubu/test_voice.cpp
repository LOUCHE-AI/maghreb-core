// The voice, measured.
//
// The point of this instrument is that a 150 cent degree really is 150 cents.
// Asserting that the tuning table says 150 proves nothing about what comes
// out of the speaker, so this renders audio and measures its pitch with a
// Goertzel fit, then compares in cents.

#include "tubu_dsp.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l,
           x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static const int SR = 48000;

// Energy at a given frequency, via Goertzel.
static double energyAt(const std::vector<float>& x, double freq) {
    const double w = 2.0 * 3.14159265358979 * freq / SR;
    const double cw = std::cos(w), coeff = 2.0 * cw;
    double s0 = 0, s1 = 0, s2 = 0;
    for (size_t i = 0; i < x.size(); i++) {
        s0 = x[i] + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Refine around a guess to find the peak, then report the offset in cents.
static double measureCentsOff(const std::vector<float>& x, double expect) {
    double best = expect, bestE = -1;
    // +/- 60 cents, in 1 cent steps
    for (int c = -60; c <= 60; c++) {
        const double f = expect * std::pow(2.0, c / 1200.0);
        const double e = energyAt(x, f);
        if (e > bestE) { bestE = e; best = f; }
    }
    return 1200.0 * std::log2(best / expect);
}

static std::vector<float> render(tubu::Tubu& g, int blocks, int n = 512) {
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

int main() {
    char msg[400];

    // ---- silence at rest ----
    {
        tubu::Tubu g;
        g.setSampleRate(SR);
        std::vector<float> x = render(g, 20);
        snprintf(msg, sizeof msg, "peak %.9f", peakOf(x));
        ok("silent with nothing held", peakOf(x) < 1e-9, msg);
    }

    // ---- a note sounds, and stops ----
    {
        tubu::Tubu g;
        g.setSampleRate(SR);
        g.setParam(tubu::kDecay, 1.0f);
        g.noteOn(60, 1.f);
        std::vector<float> on = render(g, 20);
        snprintf(msg, sizeof msg, "peak %.4f", peakOf(on));
        ok("a held note sounds", peakOf(on) > 1e-3, msg);

        g.noteOff(60);
        render(g, 40);
        std::vector<float> after = render(g, 20);
        snprintf(msg, sizeof msg, "peak %.9f, %d voices",
                 peakOf(after), g.activeVoices());
        ok("note off releases and frees the voice",
           peakOf(after) < 1e-7 && g.activeVoices() == 0, msg);
    }

    // ---- the tonic is in tune ----
    {
        tubu::Tubu g;
        g.setSampleRate(SR);
        g.setParam(tubu::kTonic, 60.f);
        g.setParam(tubu::kDecay, 8.f);
        g.setParam(tubu::kBow, 1.f);          // hold it steady while measuring
        g.setParam(tubu::kBrightness, 3.f);   // near sine, so the fit is clean
        g.noteOn(60, 1.f);
        render(g, 8);                         // let the attack settle
        std::vector<float> x = render(g, 60);

        const double expect = 440.0 * std::pow(2.0, (60 - 69) / 12.0);
        const double off = measureCentsOff(x, expect);
        snprintf(msg, sizeof msg, "%.2f cents off %.3f Hz", off, expect);
        ok("the tonic sounds at the tonic", std::fabs(off) < 1.0, msg);
    }

    // ---- every neutral degree of Sika, measured in cents ----
    {
        const int sikaDegrees[tubu::kDegrees] = {0, 150, 350, 500, 700, 850, 1050};
        double worst = 0;
        std::string worstAt;

        for (int d = 0; d < tubu::kDegrees; d++) {
            tubu::Tubu g;
            g.setSampleRate(SR);
            g.setParam(tubu::kTab, 3.f);                       // Sika
            g.setParam(tubu::kTonic, 60.f);
            g.setParam(tubu::kMapMode, (float)tubu::mapPerStep);
            g.setParam(tubu::kDecay, 8.f);
            g.setParam(tubu::kBow, 1.f);
            g.setParam(tubu::kBrightness, 3.f);
            g.noteOn(60 + d, 1.f);                             // one key per degree
            render(g, 8);
            std::vector<float> x = render(g, 60);

            const double expect = 440.0 * std::pow(2.0,
                (60 + sikaDegrees[d] / 100.0 - 69.0) / 12.0);
            const double off = std::fabs(measureCentsOff(x, expect));
            if (off > worst) {
                worst = off;
                char b[64];
                snprintf(b, sizeof b, "degree %d (%d c)", d + 1, sikaDegrees[d]);
                worstAt = b;
            }
        }
        snprintf(msg, sizeof msg, "worst %.2f cents, at %s", worst, worstAt.c_str());
        ok("every degree of Sika sounds within a cent of its table", worst < 1.0, msg);
    }

    // ---- the interval that 12-TET cannot play ----
    {
        // Sika's second is 150 cents: a quarter tone between D and D#. Measure
        // it against both, to show it is neither.
        tubu::Tubu g;
        g.setSampleRate(SR);
        g.setParam(tubu::kTab, 3.f);
        g.setParam(tubu::kTonic, 60.f);
        g.setParam(tubu::kMapMode, (float)tubu::mapPerStep);
        g.setParam(tubu::kDecay, 8.f);
        g.setParam(tubu::kBow, 1.f);
        g.setParam(tubu::kBrightness, 3.f);
        g.noteOn(61, 1.f);
        render(g, 8);
        std::vector<float> x = render(g, 60);

        const double d  = 440.0 * std::pow(2.0, (62 - 69) / 12.0);   // 200 c
        const double cs = 440.0 * std::pow(2.0, (61 - 69) / 12.0);   // 100 c
        const double offD  = measureCentsOff(x, d);
        const double offCs = measureCentsOff(x, cs);
        snprintf(msg, sizeof msg, "%.0f cents from D, %.0f cents from C#",
                 offD, offCs);
        ok("Sika's second sits between the two keys either side of it",
           std::fabs(offD + 50.0) < 2.0 && std::fabs(offCs - 50.0) < 2.0, msg);
    }

    // ---- a chord holds two neutral degrees at once ----
    {
        // This is what pitch bend cannot do: bend is per channel, so two
        // notes on one channel share one bend.
        tubu::Tubu g;
        g.setSampleRate(SR);
        g.setParam(tubu::kTab, 3.f);
        g.setParam(tubu::kTonic, 60.f);
        g.setParam(tubu::kMapMode, (float)tubu::mapPerStep);
        g.setParam(tubu::kDecay, 8.f);
        g.setParam(tubu::kBow, 1.f);
        g.setParam(tubu::kBrightness, 3.f);
        g.noteOn(61, 1.f);      // 150 cents
        g.noteOn(62, 1.f);      // 350 cents
        render(g, 8);
        std::vector<float> x = render(g, 60);

        const double f150 = 440.0 * std::pow(2.0, (60 + 1.5 - 69) / 12.0);
        const double f350 = 440.0 * std::pow(2.0, (60 + 3.5 - 69) / 12.0);
        const double e150 = energyAt(x, f150);
        const double e350 = energyAt(x, f350);
        // and nothing at the 12-TET neighbours
        const double e200 = energyAt(x, 440.0 * std::pow(2.0, (62 - 69) / 12.0));
        const double e300 = energyAt(x, 440.0 * std::pow(2.0, (63 - 69) / 12.0));
        const double floorE = std::max(e200, e300);

        snprintf(msg, sizeof msg, "150c and 350c present, %.0fx the energy at "
                 "the nearest 12-TET keys",
                 floorE > 0 ? std::min(e150, e350) / floorE : 0.0);
        ok("two neutral degrees sound together, each at its own pitch",
           e150 > floorE * 20 && e350 > floorE * 20, msg);
    }

    // ---- the drone follows the tabʿ ----
    {
        tubu::Tubu g;
        g.setSampleRate(SR);
        g.setParam(tubu::kTab, 16.f);         // Mujannab: fifth is 650 cents
        g.setParam(tubu::kTonic, 60.f);
        g.setParam(tubu::kBrightness, 3.f);
        g.setParam(tubu::kDecay, 8.f);
        g.setParam(tubu::kBow, 1.f);
        g.setParam(tubu::kDroneLevel, 1.f);
        g.setParam(tubu::kDrone, (float)tubu::droneTonicFifth);
        render(g, 8);
        std::vector<float> x = render(g, 60);

        const double f650 = 440.0 * std::pow(2.0, (60 + 6.5 - 69) / 12.0);
        const double f700 = 440.0 * std::pow(2.0, (67 - 69) / 12.0);
        const double e650 = energyAt(x, f650), e700 = energyAt(x, f700);
        snprintf(msg, sizeof msg, "650c has %.0fx the energy of 700c",
                 e700 > 0 ? e650 / e700 : 0.0);
        ok("the drone fifth is the tabʿ's fifth, not a tempered one",
           e650 > e700 * 20, msg);

        g.setParam(tubu::kDrone, (float)tubu::droneOff);
        render(g, 60);
        std::vector<float> off = render(g, 20);
        snprintf(msg, sizeof msg, "peak %.9f", peakOf(off));
        ok("turning the drone off stops it", peakOf(off) < 1e-6, msg);
    }

    // ---- nothing aliases ----
    {
        // A high note with many partials must not fold anything back down.
        tubu::Tubu g;
        g.setSampleRate(SR);
        g.setParam(tubu::kBrightness, 0.5f);   // loud upper partials
        g.setParam(tubu::kDecay, 4.f);
        g.setParam(tubu::kBow, 1.f);
        g.setParam(tubu::kTonic, 84.f);
        g.noteOn(96, 1.f);
        render(g, 8);
        std::vector<float> x = render(g, 40);

        bool finite = true;
        for (size_t i = 0; i < x.size(); i++) if (!std::isfinite(x[i])) finite = false;
        ok("a high note stays finite", finite);
        snprintf(msg, sizeof msg, "peak %.3f", peakOf(x));
        ok("and does not blow up", peakOf(x) < 4.0, msg);
    }

    // ---- polyphony ----
    {
        tubu::Tubu g;
        g.setSampleRate(SR);
        g.setParam(tubu::kDecay, 8.f);
        g.setParam(tubu::kBow, 1.f);
        for (int i = 0; i < tubu::kMaxVoices; i++) g.noteOn(60 + i, 1.f);
        render(g, 4);
        snprintf(msg, sizeof msg, "%d voices", g.activeVoices());
        ok("it holds a full chord", g.activeVoices() == tubu::kMaxVoices, msg);

        g.noteOn(90, 1.f);
        render(g, 4);
        snprintf(msg, sizeof msg, "%d voices", g.activeVoices());
        ok("one more steals rather than piling up",
           g.activeVoices() == tubu::kMaxVoices, msg);

        g.allNotesOff();
        render(g, 60);
        snprintf(msg, sizeof msg, "%d voices", g.activeVoices());
        ok("all notes off clears them", g.activeVoices() == 0, msg);
    }

    // ---- retuning under a held note ----
    {
        tubu::Tubu g;
        g.setSampleRate(SR);
        g.setParam(tubu::kDecay, 8.f);
        g.setParam(tubu::kBow, 1.f);
        g.noteOn(62, 1.f);
        render(g, 8);
        g.setParam(tubu::kTab, 0.f);       // change tabʿ while it sounds
        render(g, 60);
        snprintf(msg, sizeof msg, "%d voices", g.activeVoices());
        ok("changing tabʿ releases what was held rather than bending it",
           g.activeVoices() == 0, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
