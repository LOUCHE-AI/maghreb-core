// The tuning engine, checked in cents and hertz.
//
// A quantiser is exactly the kind of thing that looks right and is silently
// a semitone out, so every claim here is measured rather than assumed.

#include "tubu_tuning.h"
#include <cstdio>
#include <cmath>
#include <set>
#include <string>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l,
           x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

int main() {
    char msg[512];

    // ---- the tables themselves ----
    {
        bool tonicZero = true, ordered = true, inRange = true;
        std::string bad;
        for (int t = 0; t < tubu::kNumTubu; t++) {
            if (tubu::kFactory[t][0] != 0) tonicZero = false;
            for (int d = 0; d < tubu::kDegrees; d++) {
                if (tubu::kFactory[t][d] < 0 || tubu::kFactory[t][d] > 1200)
                    inRange = false;
                if (d > 0 && tubu::kFactory[t][d] < tubu::kFactory[t][d - 1]) {
                    ordered = false;
                    if (bad.empty()) bad = tubu::kNames[t];
                }
            }
        }
        ok("every tabʿ starts on its tonic", tonicZero);
        ok("every degree is inside an octave", inRange);
        ok("no tabʿ steps backwards", ordered, bad);
    }

    // the one known defect, asserted so it cannot change unnoticed
    {
        std::set<int> distinct;
        for (int d = 0; d < tubu::kDegrees; d++)
            distinct.insert(tubu::kFactory[14][d]);
        snprintf(msg, sizeof msg, "%d distinct of %d degrees",
                 (int)distinct.size(), tubu::kDegrees);
        ok("Mazmum still has the duplicate degrees the binary holds",
           distinct.size() == 5, msg);

        int others = 0;
        for (int t = 0; t < tubu::kNumTubu; t++) {
            std::set<int> s;
            for (int d = 0; d < tubu::kDegrees; d++) s.insert(tubu::kFactory[t][d]);
            if (s.size() != tubu::kDegrees) others++;
        }
        snprintf(msg, sizeof msg, "%d tabʿ affected", others);
        ok("and it is the only one affected", others == 1, msg);
    }

    // ---- neutral intervals are actually present ----
    {
        int withNeutral = 0;
        tubu::Tuning g;
        for (int t = 0; t < tubu::kNumTubu; t++) {
            g.setTab(t);
            if (g.neutralCount() > 0) withNeutral++;
        }
        g.setTab(3);
        // Sika is 0 150 350 500 700 850 1050 - four of its seven degrees have
        // no key on a piano
        snprintf(msg, sizeof msg, "%d of %d tabʿ, Sika has %d",
                 withNeutral, tubu::kNumTubu, g.neutralCount());
        ok("most tabʿ carry intervals 12-TET cannot play",
           withNeutral >= 10 && g.neutralCount() == 4, msg);
    }

    // ---- the tonic is exactly the tonic ----
    {
        tubu::Tuning g;
        g.setTonic(60);
        g.setMode(tubu::mapPerKey);
        const double middleC = 440.0 * std::pow(2.0, (60 - 69) / 12.0);
        snprintf(msg, sizeof msg, "%.4f Hz vs %.4f Hz", g.frequencyFor(60), middleC);
        ok("the tonic key sounds the tonic exactly",
           std::fabs(g.frequencyFor(60) - middleC) < 1e-9, msg);
    }

    // ---- Sika's neutral third is where it should be ----
    {
        tubu::Tuning g;
        g.setTab(3);                       // Sika: 0 150 350 500 700 850 1050
        g.setTonic(60);
        g.setMode(tubu::mapPerKey);
        // third degree = key 62 in per-key mapping, 350 cents above the tonic
        const double cents = g.centsFor(62);
        const double ratio = g.frequencyFor(62) / g.frequencyFor(60);
        const double inCents = 1200.0 * std::log2(ratio);
        snprintf(msg, sizeof msg, "%.0f cents asked, %.2f cents measured",
                 cents, inCents);
        ok("Sika's neutral third lands 350 cents up, not 300 or 400",
           std::fabs(cents - 350.0) < 1e-9 && std::fabs(inCents - 350.0) < 0.01, msg);
    }

    // ---- octaves ----
    {
        tubu::Tuning g;
        g.setTab(3);
        g.setTonic(60);
        g.setMode(tubu::mapPerKey);
        const double lo = g.frequencyFor(60), hi = g.frequencyFor(72);
        snprintf(msg, sizeof msg, "%.3f Hz -> %.3f Hz, ratio %.6f", lo, hi, hi / lo);
        ok("an octave up is exactly double", std::fabs(hi / lo - 2.0) < 1e-9, msg);
    }

    // ---- the mapping modes differ in the way they claim to ----
    {
        tubu::Tuning g;
        g.setTab(3);
        g.setTonic(60);

        // The module's own per-key mapping uses the pitch class modulo seven,
        // so keys 67..71 repeat degrees 0..4 inside the same octave and the
        // keyboard stops rising. Asserted because it is the module's
        // behaviour, not because it is desirable.
        g.setMode(tubu::mapPerKey);
        const double k66 = g.centsFor(66), k67 = g.centsFor(67);
        snprintf(msg, sizeof msg, "key 66 = %.0f c, key 67 = %.0f c", k66, k67);
        ok("per-key mapping folds back after seven keys, as the module does",
           k67 < k66, msg);

        // the corrected mapping never goes backwards
        g.setMode(tubu::mapPerStep);
        bool rising = true;
        double prev = -1e9;
        for (int p = 48; p <= 84; p++) {
            const double c = g.centsFor(p);
            if (c <= prev) rising = false;
            prev = c;
        }
        snprintf(msg, sizeof msg, "keys 48..84, %.0f c to %.0f c",
                 g.centsFor(48), g.centsFor(84));
        ok("per-step mapping rises across the whole keyboard", rising, msg);

        // and it still puts the tonic on the tonic, an octave every 7 keys
        snprintf(msg, sizeof msg, "key 67 = %.0f c", g.centsFor(67));
        ok("per-step reaches the octave seven keys up",
           std::fabs(g.centsFor(67) - 1200.0) < 1e-9, msg);
    }

    // ---- nearest mode pulls to the closest degree ----
    {
        tubu::Tuning g;
        g.setTab(3);                       // 0 150 350 500 700 850 1050
        g.setTonic(60);
        g.setMode(tubu::mapNearest);
        // key 62 is 200 cents, whose closest Sika degree is 150
        snprintf(msg, sizeof msg, "200 c in -> %.0f c out", g.centsFor(62));
        ok("nearest pulls 200 cents onto Sika's 150", g.centsFor(62) == 150.0, msg);
        // key 71 is 1100 cents, closest is 1050
        snprintf(msg, sizeof msg, "1100 c in -> %.0f c out", g.centsFor(71));
        ok("nearest pulls 1100 cents onto Sika's 1050", g.centsFor(71) == 1050.0, msg);
        // key 72 is the octave
        snprintf(msg, sizeof msg, "key 72 -> %.0f c", g.centsFor(72));
        ok("nearest lets the octave through", g.centsFor(72) == 1200.0, msg);
    }

    // ---- editing and reverting ----
    {
        tubu::Tuning g;
        g.setTab(3);
        ok("a fresh tabʿ is unedited", !g.edited());
        g.setDegreeCents(1, 160);
        snprintf(msg, sizeof msg, "second = %d c", g.degreeCents(1));
        ok("a degree can be retuned", g.edited() && g.degreeCents(1) == 160, msg);
        g.revert();
        snprintf(msg, sizeof msg, "second = %d c", g.degreeCents(1));
        ok("revert puts the factory tabʿ back",
           !g.edited() && g.degreeCents(1) == 150, msg);

        // switching tabʿ must not carry an edit across
        g.setDegreeCents(1, 175);
        g.setTab(5);
        ok("changing tabʿ drops the edits", !g.edited());

        // out of range is clamped, not wrapped
        g.setDegreeCents(2, 99999);
        g.setDegreeCents(3, -50);
        snprintf(msg, sizeof msg, "%d c and %d c", g.degreeCents(2), g.degreeCents(3));
        ok("silly cent values clamp inside the octave",
           g.degreeCents(2) == 1200 && g.degreeCents(3) == 0, msg);
    }

    // ---- the drone ----
    {
        tubu::Tuning g;
        g.setTab(16);                      // Mujannab: fifth is 650, not 700
        g.setTonic(60);
        const double ratio = g.droneFrequency(true) / g.droneFrequency(false);
        const double cents = 1200.0 * std::log2(ratio);
        snprintf(msg, sizeof msg, "%.2f cents above the tonic", cents);
        ok("the drone fifth follows the tabʿ, not a plain 700 cents",
           std::fabs(cents - 650.0) < 0.01, msg);
    }

    // ---- range ----
    {
        tubu::Tuning g;
        g.setTonic(60);

        // Seven keys to the octave means the top of the keyboard runs away.
        // The check exists so the voice is never handed a frequency above the
        // sample rate; without it key 127 asks for roughly 200 kHz.
        g.setTab(3);
        g.setMode(tubu::mapPerStep);
        snprintf(msg, sizeof msg, "key 127 resolves to MIDI %.1f",
                 g.resolvedMidi(127));
        ok("per-step marks runaway keys unplayable rather than screaming",
           !g.playable(127), msg);
        ok("and keeps the ones that are in range", g.playable(60) && g.playable(67));

        bool sane = true;
        int dropped = 0;
        for (int t = 0; t < tubu::kNumTubu; t++) {
            g.setTab(t);
            for (int m = 0; m < tubu::kNumMapModes; m++) {
                g.setMode(m);
                for (int p = 0; p <= 127; p++) {
                    if (!g.playable(p)) { dropped++; continue; }
                    const double f = g.frequencyFor(p);
                    if (!std::isfinite(f) || f <= 0.0 || f > 20000.0) sane = false;
                }
            }
        }
        snprintf(msg, sizeof msg, "%d key/tabʿ/mode combinations dropped", dropped);
        ok("every playable key, tabʿ and mode gives a usable frequency", sane, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
