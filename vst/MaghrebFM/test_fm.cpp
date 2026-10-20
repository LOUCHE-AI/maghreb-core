// Maghreb FM, measured.
//
// The engine is a port and the stroke table is editable, so the two things
// worth proving are that the factory strokes still sound like themselves and
// that an edit actually reaches the sound.

#include "fm_dsp.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static const int SR = 44100;

static double peakOf(const std::vector<float>& v) {
    double p = 0;
    for (size_t i = 0; i < v.size(); i++) p = std::max(p, (double)std::fabs(v[i]));
    return p;
}
// Crossing rate over the part that is actually sounding.
//
// Measuring across a fixed window instead makes every comparison a comparison
// of decay length: a ka stops after fifty milliseconds, so nine tenths of the
// window is silence and it reads as lower-pitched than a doum. Everything the
// kits and the filters change also changes decay, so the fixed-window version
// got three of these tests wrong in the same way.
static double zcr(const std::vector<float>& x) {
    const double pk = peakOf(x);
    if (pk <= 0) return 0;
    size_t end = 0;
    for (size_t i = 0; i < x.size(); i++)
        if (std::fabs(x[i]) > pk * 0.05) end = i;
    end = std::min(end + 1, x.size());
    if (end < 64) return 0;
    int c = 0;
    for (size_t i = 1; i < end; i++) if ((x[i] < 0) != (x[i - 1] < 0)) c++;
    return (double)c / end * SR * 0.5;
}
// how long until it falls below -40 dB of its own peak
static double tailSeconds(const std::vector<float>& x) {
    const double pk = peakOf(x);
    if (pk <= 0) return 0;
    for (size_t i = x.size(); i-- > 0;)
        if (std::fabs(x[i]) > pk * 0.01) return (double)i / SR;
    return 0;
}

static std::vector<float> strike(mfm::MaghrebFM& g, int note, float vel = 1.f,
                                 double seconds = 1.5) {
    const int n = 256;
    const int blocks = (int)(seconds * SR / n);
    std::vector<float> out;
    std::vector<float> l(n), r(n), lr(n);
    g.noteOn(note, vel);
    for (int b = 0; b < blocks; b++) {
        g.process(l.data(), r.data(), n);
        for (int i = 0; i < n; i++) out.push_back(l[i] + r[i]);
    }
    return out;
}

int main() {
    char msg[400];

    // ---- silence, then a hit ----
    {
        mfm::MaghrebFM g;
        g.setSampleRate(SR);
        std::vector<float> l(SR), r(SR);
        g.process(l.data(), r.data(), SR);
        std::vector<float> quiet(l.begin(), l.end());
        snprintf(msg, sizeof msg, "peak %.9f", peakOf(quiet));
        ok("silent until struck", peakOf(quiet) < 1e-12, msg);

        std::vector<float> x = strike(g, 36);
        snprintf(msg, sizeof msg, "peak %.3f", peakOf(x));
        ok("note 36 makes a doum", peakOf(x) > 1e-3, msg);
    }

    // ---- all eleven strokes, on their eleven notes ----
    {
        mfm::MaghrebFM g;
        g.setSampleRate(SR);
        bool allSound = true, allFinite = true;
        double lo = 1e9, hi = 0;
        for (int i = 0; i < mfm::kNumStrokes; i++) {
            std::vector<float> x = strike(g, mfm::kFirstNote + i);
            const double pk = peakOf(x);
            lo = std::min(lo, pk); hi = std::max(hi, pk);
            if (pk < 1e-3) allSound = false;
            for (size_t k = 0; k < x.size(); k++) if (!std::isfinite(x[k])) allFinite = false;
        }
        snprintf(msg, sizeof msg, "peaks %.3f .. %.3f across notes 36-46", lo, hi);
        ok("every stroke sounds on its own note", allSound, msg);
        ok("and none produces NaN or inf", allFinite);
        ok("nothing clips past unity", hi <= 1.0, msg);

        // a note outside the mapped range must do nothing
        std::vector<float> none = strike(g, 60);
        snprintf(msg, sizeof msg, "peak %.9f", peakOf(none));
        ok("a note outside 36-46 is ignored", peakOf(none) < 1e-12, msg);
    }

    // ---- the strokes are distinguishable ----
    {
        mfm::MaghrebFM g;
        g.setSampleRate(SR);
        g.setParam(mfm::kHumanize, 0.f);
        const double doum = zcr(strike(g, 36, 1.f, 0.5));
        const double tak  = zcr(strike(g, 39, 1.f, 0.5));
        const double ka   = zcr(strike(g, 41, 1.f, 0.5));
        snprintf(msg, sizeof msg, "doum %.0f, tak %.0f, ka %.0f Hz", doum, tak, ka);
        ok("doum is lowest, ka is highest", doum < tak && tak < ka, msg);
    }

    // ---- the three kits ----
    {
        mfm::MaghrebFM g;
        g.setSampleRate(SR);
        g.setParam(mfm::kHumanize, 0.f);
        double c[mfm::kNumKits];
        for (int k = 0; k < mfm::kNumKits; k++) {
            g.setParam(mfm::kKit, (float)k);
            c[k] = zcr(strike(g, 36, 1.f, 0.5));
        }
        snprintf(msg, sizeof msg, "bendir %.0f, darbuka %.0f, tar %.0f Hz",
                 c[0], c[1], c[2]);
        ok("the kits are pitched apart, tar highest", c[0] < c[1] && c[1] < c[2], msg);
    }

    // ---- the global controls do what they say ----
    {
        mfm::MaghrebFM g;
        g.setSampleRate(SR);
        g.setParam(mfm::kHumanize, 0.f);

        const double normal = tailSeconds(strike(g, 36, 1.f, 3.0));
        g.setParam(mfm::kDecay, 3.f);
        const double longer = tailSeconds(strike(g, 36, 1.f, 3.0));
        g.setParam(mfm::kDecay, 0.3f);
        const double shorter = tailSeconds(strike(g, 36, 1.f, 3.0));
        snprintf(msg, sizeof msg, "%.2f s at 1x, %.2f s at 3x, %.2f s at 0.3x",
                 normal, longer, shorter);
        ok("Decay lengthens and shortens the tail",
           longer > normal * 1.5 && shorter < normal * 0.7, msg);
        g.setParam(mfm::kDecay, 1.f);

        const double at0 = zcr(strike(g, 36, 1.f, 0.5));
        g.setParam(mfm::kTune, 12.f);
        const double at12 = zcr(strike(g, 36, 1.f, 0.5));
        snprintf(msg, sizeof msg, "%.0f Hz -> %.0f Hz", at0, at12);
        ok("Tune up an octave roughly doubles the pitch",
           at12 > at0 * 1.6 && at12 < at0 * 2.5, msg);
        g.setParam(mfm::kTune, 0.f);

        const double loud = peakOf(strike(g, 36, 1.f, 1.0));
        g.setParam(mfm::kOutput, -12.f);
        const double soft = peakOf(strike(g, 36, 1.f, 1.0));
        snprintf(msg, sizeof msg, "%.3f -> %.3f", loud, soft);
        ok("Output trims the whole thing", soft < loud * 0.35, msg);
        g.setParam(mfm::kOutput, 0.f);
    }

    // ---- velocity ----
    {
        mfm::MaghrebFM g;
        g.setSampleRate(SR);
        g.setParam(mfm::kHumanize, 0.f);
        const double hard = peakOf(strike(g, 39, 1.0f, 0.6));
        const double soft = peakOf(strike(g, 39, 0.3f, 0.6));
        snprintf(msg, sizeof msg, "%.3f hard, %.3f soft", hard, soft);
        ok("a softer strike is quieter", soft < hard * 0.6, msg);

        // and duller, because velocity opens the FM index
        g.setParam(mfm::kVelFM, 1.f);
        const double hz1 = zcr(strike(g, 39, 1.0f, 0.4));
        const double hz03 = zcr(strike(g, 39, 0.3f, 0.4));
        snprintf(msg, sizeof msg, "%.0f Hz hard, %.0f Hz soft", hz1, hz03);
        ok("and duller, not merely quieter", hz03 < hz1 * 0.95, msg);
    }

    // ---- the filters ----
    {
        mfm::MaghrebFM g;
        g.setSampleRate(SR);
        g.setParam(mfm::kHumanize, 0.f);
        const double open = zcr(strike(g, 41, 1.f, 0.4));    // ka, bright
        g.setParam(mfm::kLowpass, 500.f);
        const double closed = zcr(strike(g, 41, 1.f, 0.4));
        snprintf(msg, sizeof msg, "%.0f Hz -> %.0f Hz", open, closed);
        ok("the lowpass takes the top off", closed < open * 0.7, msg);
        g.setParam(mfm::kLowpass, 18000.f);

        const double full = peakOf(strike(g, 36, 1.f, 1.0));  // doum, low
        g.setParam(mfm::kHighpass, 2000.f);
        const double thin = peakOf(strike(g, 36, 1.f, 1.0));
        snprintf(msg, sizeof msg, "%.3f -> %.3f", full, thin);
        ok("the highpass takes the bottom out", thin < full * 0.5, msg);
        g.setParam(mfm::kHighpass, 5.f);
    }

    // ---- editing a stroke ----
    {
        mfm::MaghrebFM g;
        g.setSampleRate(SR);
        g.setParam(mfm::kHumanize, 0.f);
        ok("the table starts as the factory one", !g.strokeEdited(0));

        const double before = zcr(strike(g, 36, 1.f, 0.5));
        g.setParam(mfm::kSelect, 0.f);
        g.setParam(mfm::kSFreq, 300.f);              // doum at 300 Hz
        const double after = zcr(strike(g, 36, 1.f, 0.5));
        snprintf(msg, sizeof msg, "%.0f Hz -> %.0f Hz", before, after);
        ok("an edit reaches the sound on the next strike", after > before * 1.8, msg);
        ok("and the stroke is marked as edited", g.strokeEdited(0));
        ok("while its neighbours are not", !g.strokeEdited(1) && !g.strokeEdited(2));

        // the edit must follow the selection, not leak onto another stroke
        g.setParam(mfm::kSelect, 3.f);
        g.setParam(mfm::kSFreq, 900.f);
        ok("editing after switching selection hits the new stroke",
           g.strokeEdited(3) && g.stroke(0).freq == 300.f &&
           g.stroke(3).freq == 900.f,
           std::to_string(g.stroke(0).freq) + " and " + std::to_string(g.stroke(3).freq));

        g.resetStroke(0);
        ok("resetting one stroke puts it back and leaves the other",
           !g.strokeEdited(0) && g.strokeEdited(3));
        g.resetAll();
        ok("reset all clears every edit", !g.strokeEdited(3));
    }

    // ---- the table survives a save and load ----
    {
        mfm::MaghrebFM a, b;
        a.setSampleRate(SR); b.setSampleRate(SR);
        a.setParam(mfm::kSelect, 5.f);
        a.setParam(mfm::kSFreq, 123.f);
        a.setParam(mfm::kSIndex, 4.5f);
        a.setParam(mfm::kSPan, -0.8f);

        std::vector<float> blob(mfm::MaghrebFM::kTableFloats);
        a.saveTable(blob.data());
        b.loadTable(blob.data());
        snprintf(msg, sizeof msg, "freq %.0f, index %.2f, pan %.2f",
                 b.stroke(5).freq, b.stroke(5).index, b.strokePan(5));
        ok("a saved table comes back exactly",
           b.stroke(5).freq == 123.f && b.stroke(5).index == 4.5f &&
           b.strokePan(5) == -0.8f, msg);
    }

    // ---- polyphony ----
    {
        mfm::MaghrebFM g;
        g.setSampleRate(SR);
        const int n = 256;
        std::vector<float> l(n), r(n);
        for (int i = 0; i < mfm::kNumStrokes; i++) g.noteOn(mfm::kFirstNote + i, 1.f);
        g.process(l.data(), r.data(), n);
        snprintf(msg, sizeof msg, "%d voices", g.activeVoices());
        ok("a whole kit can sound at once", g.activeVoices() >= 8, msg);

        for (int i = 0; i < 40; i++) g.noteOn(36, 1.f);
        g.process(l.data(), r.data(), n);
        snprintf(msg, sizeof msg, "%d voices of %d", g.activeVoices(), mfm::kMaxVoices);
        ok("more strikes than voices steal rather than pile up",
           g.activeVoices() <= mfm::kMaxVoices, msg);
    }

    // ---- humanize varies, and only when asked ----
    {
        mfm::MaghrebFM a;
        a.setSampleRate(SR);
        a.setParam(mfm::kHumanize, 0.f);
        const double p1 = peakOf(strike(a, 36, 1.f, 0.5));
        const double p2 = peakOf(strike(a, 36, 1.f, 0.5));
        snprintf(msg, sizeof msg, "%.6f then %.6f", p1, p2);
        ok("with humanize off two strikes are identical",
           std::fabs(p1 - p2) < 1e-6, msg);

        a.setParam(mfm::kHumanize, 1.f);
        double lo = 1e9, hi = 0;
        for (int i = 0; i < 12; i++) {
            const double p = peakOf(strike(a, 36, 1.f, 0.5));
            lo = std::min(lo, p); hi = std::max(hi, p);
        }
        snprintf(msg, sizeof msg, "%.3f .. %.3f over twelve strikes", lo, hi);
        ok("with it up they vary", hi > lo * 1.05, msg);
    }

    // ---- a sweep with everything pushed ----
    {
        mfm::MaghrebFM g;
        g.setSampleRate(SR);
        g.setParam(mfm::kDrive, 1.f);
        g.setParam(mfm::kFM, 2.f);
        g.setParam(mfm::kNoise, 2.f);
        g.setParam(mfm::kLevel, 1.f);
        g.setParam(mfm::kTune, 24.f);
        bool sane = true;
        double worst = 0;
        for (int i = 0; i < mfm::kNumStrokes; i++) {
            std::vector<float> x = strike(g, mfm::kFirstNote + i, 1.f, 0.8);
            worst = std::max(worst, peakOf(x));
            for (size_t k = 0; k < x.size(); k++)
                if (!std::isfinite(x[k])) sane = false;
        }
        snprintf(msg, sizeof msg, "worst peak %.3f", worst);
        ok("every stroke stays finite and bounded with everything pushed",
           sane && worst < 4.0, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
