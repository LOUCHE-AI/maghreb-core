// The patch page's machinery, measured.
//
// The page itself is drawing; what can be wrong underneath is whether an edit
// reaches the engine, whether it survives, and whether REVERT really goes
// back. Those are all audible claims, so they are checked by rendering.

#include "seq_dsp.h"

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

static const int SR = 44100;

// one stroke on the DX engine, rendered
static std::vector<float> strike(mseq::MaghrebSeq& g, int stroke, double seconds) {
    const int n = 256;
    const int blocks = (int)(seconds * SR / n);
    std::vector<float> out;
    std::vector<float> L(n), R(n);
    g.audition(stroke);
    for (int b = 0; b < blocks; b++) {
        g.process(L.data(), R.data(), n);
        for (int i = 0; i < n; i++) out.push_back(L[i]);
    }
    return out;
}
static double peakOf(const std::vector<float>& v) {
    double p = 0; for (float x : v) p = std::max(p, (double)std::fabs(x)); return p;
}
static double corr(const std::vector<float>& a, const std::vector<float>& b) {
    double num = 0, da = 0, db = 0;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) { num += a[i]*b[i]; da += a[i]*a[i]; db += b[i]*b[i]; }
    return num / std::max(1e-12, std::sqrt(da * db));
}
static double worstDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double w = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); i++)
        w = std::max(w, (double)std::fabs(a[i] - b[i]));
    return w;
}

static mseq::MaghrebSeq* make() {
    auto* g = new mseq::MaghrebSeq();
    g->setSampleRate(SR);
    g->setParam(mseq::kEngine, 1.f);          // DX7
    for (int l = 0; l < mseq::kLanes; l++)
        g->setParam(mseq::laneParam(l, mseq::kLaneMute), l ? 1.f : 0.f);
    g->setTransport(0, false);                 // auditions only
    return g;
}

// OP1's output level lives at byte (5-0)*21 + 16
static const int kOp1Level = 5 * 21 + 16;
static const int kOp2Level = 4 * 21 + 16;

int main() {
    char msg[400];
    const int stroke = 3;                      // T

    // ---- a fresh stroke is derived, not edited ----
    {
        mseq::MaghrebSeq* g = make();
        ok("a fresh stroke is derived", !g->strokeEdited(stroke));

        uint8_t a[156], b[156];
        g->patchFor(stroke, a);
        g->patchFor(stroke, b);
        bool same = true;
        for (int i = 0; i < 156; i++) if (a[i] != b[i]) same = false;
        ok("and asking for its bytes twice gives the same patch", same);

        int nonZero = 0;
        for (int i = 0; i < 156; i++) if (a[i]) nonZero++;
        snprintf(msg, sizeof msg, "%d non-zero bytes, algorithm %d, feedback %d",
                 nonZero, a[134] + 1, a[135]);
        ok("the derivation is a real patch", nonZero > 30 && a[134] == 4, msg);
        delete g;
    }

    // ---- an edit reaches the engine ----
    {
        mseq::MaghrebSeq* g = make();
        std::vector<float> before = strike(*g, stroke, 0.7);

        g->setPatchByte(stroke, kOp2Level, 0, 0, 99);   // silence the modulator
        ok("editing a byte marks the stroke edited", g->strokeEdited(stroke));

        std::vector<float> after = strike(*g, stroke, 0.7);
        snprintf(msg, sizeof msg, "peak %.3f -> %.3f, correlation %.3f",
                 peakOf(before), peakOf(after), corr(before, after));
        ok("and it changes what you hear",
           peakOf(after) > 1e-4 && std::fabs(corr(before, after)) < 0.98, msg);
        delete g;
    }

    // ---- REVERT really goes back ----
    {
        mseq::MaghrebSeq* g = make();
        std::vector<float> before = strike(*g, stroke, 0.7);
        g->setPatchByte(stroke, kOp2Level, 12, 0, 99);
        g->setPatchByte(stroke, kOp1Level, 40, 0, 99);
        std::vector<float> edited = strike(*g, stroke, 0.7);
        g->revertStroke(stroke);
        std::vector<float> back = strike(*g, stroke, 0.7);

        snprintf(msg, sizeof msg, "edited differs by %.3e, reverted differs by %.3e",
                 worstDiff(before, edited), worstDiff(before, back));
        ok("revert restores the derived sound exactly",
           !g->strokeEdited(stroke) && worstDiff(before, back) == 0.0 &&
           worstDiff(before, edited) > 1e-5, msg);
        delete g;
    }

    // ---- an edit on one stroke leaves the others alone ----
    //
    // Compared on two fresh engines rather than before-and-after on one. msfa's
    // FmCore keeps a shared scratch buffer, so any extra render - and measuring
    // an edited patch's peak is an extra render - perturbs the next voice by
    // about -80 dB. That is real but it is numerical, not the edit leaking, and
    // this is the comparison that tells the two apart.
    {
        mseq::MaghrebSeq* plain = make();
        std::vector<float> other = strike(*plain, 0, 0.6);

        mseq::MaghrebSeq* g = make();
        g->setPatchByte(stroke, kOp2Level, 0, 0, 99);
        std::vector<float> otherAgain = strike(*g, 0, 0.6);

        snprintf(msg, sizeof msg, "worst %.3e", worstDiff(other, otherAgain));
        ok("editing one stroke does not touch another",
           worstDiff(other, otherAgain) == 0.0 && !g->strokeEdited(0), msg);
        delete plain; delete g;
    }

    // And the size of that shared-scratch drift, stated rather than hidden: it
    // has to stay far below anything audible.
    {
        mseq::MaghrebSeq* g = make();
        std::vector<float> a = strike(*g, 0, 0.6);
        g->setPatchByte(stroke, kOp2Level, 0, 0, 99);       // forces a probe render
        std::vector<float> b = strike(*g, 0, 0.6);
        const double d = worstDiff(a, b);
        snprintf(msg, sizeof msg, "%.3e, %.1f dB below peak",
                 d, 20.0 * std::log10(std::max(1e-12, d) / std::max(1e-12, peakOf(a))));
        ok("a probe render disturbs the next voice only numerically", d < 1e-3, msg);
        delete g;
    }

    // ---- bytes are clamped to the field's own range ----
    {
        mseq::MaghrebSeq* g = make();
        g->setPatchByte(stroke, kOp1Level, 5000, 0, 99);
        uint8_t p[156]; g->patchFor(stroke, p);
        const int hi = p[kOp1Level];
        g->setPatchByte(stroke, kOp1Level, -400, 0, 99);
        g->patchFor(stroke, p);
        const int lo = p[kOp1Level];
        snprintf(msg, sizeof msg, "high clamps to %d, low clamps to %d", hi, lo);
        ok("a byte cannot be driven out of range", hi == 99 && lo == 0, msg);

        // and the engine survives whatever those extremes are
        std::vector<float> x = strike(*g, stroke, 0.5);
        bool finite = true;
        for (float v : x) if (!std::isfinite(v)) finite = false;
        ok("and the engine stays finite at the extremes", finite);
        delete g;
    }

    // ---- the waveform displays show the real operators ----
    {
        mseq::MaghrebSeq* g = make();
        float w[192];
        int sounding = 0, silent = 0;
        std::string detail;
        for (int op = 0; op < 6; op++) {
            const bool got = g->operatorWave(stroke, op, w, 192);
            double pk = 0;
            for (int i = 0; i < 192; i++) pk = std::max(pk, (double)std::fabs(w[i]));
            if (got && pk > 0.5) sounding++; else silent++;
            char b[48]; snprintf(b, sizeof b, "%d:%s ", op + 1, got ? "on" : "-");
            detail += b;
        }
        snprintf(msg, sizeof msg, "%s(%d sounding, %d silent)",
                 detail.c_str(), sounding, silent);
        // the derivation uses four operators: OP1/OP2 body, OP5/OP6 noise
        ok("four operators sound and two are silent, as the derivation says",
           sounding == 4 && silent == 2, msg);

        // silencing an operator must silence its display too
        g->setPatchByte(stroke, kOp1Level, 0, 0, 99);
        const bool got = g->operatorWave(stroke, 0, w, 192);
        ok("and an operator turned down goes quiet on the page", !got);
        delete g;
    }

    // ---- edits survive a save and reload of the bytes ----
    {
        mseq::MaghrebSeq* g = make();
        g->setPatchByte(stroke, kOp2Level, 31, 0, 99);
        uint8_t saved[156];
        std::memcpy(saved, g->editedBytes(stroke), 156);
        std::vector<float> before = strike(*g, stroke, 0.6);

        mseq::MaghrebSeq* h = make();
        h->loadEdited(stroke, saved);
        std::vector<float> after = strike(*h, stroke, 0.6);
        snprintf(msg, sizeof msg, "worst %.3e", worstDiff(before, after));
        ok("an edited patch reloads to the same sound",
           h->strokeEdited(stroke) && worstDiff(before, after) == 0.0, msg);
        delete g; delete h;
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
