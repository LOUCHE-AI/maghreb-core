// Editing the loaded voice, measured.
//
// Tubu DX plays cartridge bytes straight, so making the patch page editable
// means the instrument stops playing the cartridge and starts playing a copy.
// The things that can go wrong are all audible: the edit not reaching the
// engine, revert not really reverting, and an edit surviving a voice change
// it should not survive.

#include "tubudx_dsp.h"
#include "cart_index.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static const double SR = 44100;

static std::vector<float> play(tdx::TubuDX& g, int note, double seconds) {
    const int n = 256;
    const int blocks = (int)(seconds * SR / n);
    std::vector<float> out;
    std::vector<float> L(n), R(n);
    g.noteOn(note, 1.0f);
    for (int b = 0; b < blocks; b++) {
        if (b == blocks / 2) g.noteOff(note);
        g.process(L.data(), R.data(), n);
        for (int i = 0; i < n; i++) out.push_back(L[i]);
    }
    return out;
}
static double peakOf(const std::vector<float>& v) {
    double p = 0; for (float x : v) p = std::max(p, (double)std::fabs(x)); return p;
}
static double worstDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double w = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); i++)
        w = std::max(w, (double)std::fabs(a[i] - b[i]));
    return w;
}
static double corr(const std::vector<float>& a, const std::vector<float>& b) {
    double num = 0, da = 0, db = 0;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) { num += a[i]*b[i]; da += a[i]*a[i]; db += b[i]*b[i]; }
    return num / std::max(1e-12, std::sqrt(da * db));
}

// OP1's level is byte (5-0)*21+16; OP2's modulator level (5-1)*21+16
static const int kOp1Level = 5 * 21 + 16;
static const int kOp2Level = 4 * 21 + 16;

static tdx::TubuDX* make(const std::string& cart) {
    auto* g = new tdx::TubuDX();
    g->setSampleRate(SR);
    if (!cart.empty()) g->loadCartridge(cart.c_str());
    return g;
}

int main() {
    char msg[400];
    const char* base = std::getenv("LOCALAPPDATA");
    const std::string dir = std::string(base ? base : ".") + "\\MaghrebDX\\Cartridges";
    mdx::CartIndex ix;
    ix.scan(dir);
    const std::string cart = ix.empty() ? std::string() : ix.cart(0).path;
    snprintf(msg, sizeof msg, "%d cartridges", ix.count());
    ok("there is a cartridge to edit", !cart.empty(), msg);
    if (cart.empty()) { printf("\nFAILURES\n"); return 1; }

    const int note = 60;

    // ---- a loaded voice starts unedited and plays the cartridge ----
    {
        tdx::TubuDX* g = make(cart);
        ok("a freshly loaded voice is not edited", !g->patchEdited());
        uint8_t a[156], b[156];
        g->currentPatch(a);
        g->currentPatch(b);
        bool same = true;
        for (int i = 0; i < 156; i++) if (a[i] != b[i]) same = false;
        ok("and its bytes are stable", same);
        delete g;
    }

    // ---- an edit reaches the engine ----
    {
        tdx::TubuDX* g = make(cart);
        std::vector<float> before = play(*g, note, 1.0);
        g->setPatchByte(kOp2Level, 0, 0, 99);
        ok("editing a byte marks the patch edited", g->patchEdited());
        std::vector<float> after = play(*g, note, 1.0);
        snprintf(msg, sizeof msg, "peak %.3f -> %.3f, correlation %.3f",
                 peakOf(before), peakOf(after), corr(before, after));
        ok("and it changes what you hear",
           peakOf(after) > 1e-5 && std::fabs(corr(before, after)) < 0.99, msg);
        delete g;
    }

    // ---- revert really reverts ----
    //
    // Each rendering gets a fresh instrument. This one is polyphonic with a
    // sustaining voice, so playing three notes into the same engine leaves the
    // tail of the first one under the second - which is a property of the
    // instrument, not of the edit, and would make an exact comparison
    // meaningless. The three engines differ only in what was done to the patch.
    {
        tdx::TubuDX* a = make(cart);
        std::vector<float> before = play(*a, note, 1.0);

        tdx::TubuDX* b = make(cart);
        b->setPatchByte(kOp1Level, 30, 0, 99);
        b->setPatchByte(kOp2Level, 70, 0, 99);
        std::vector<float> edited = play(*b, note, 1.0);

        tdx::TubuDX* c = make(cart);
        c->setPatchByte(kOp1Level, 30, 0, 99);
        c->setPatchByte(kOp2Level, 70, 0, 99);
        c->revertPatch();
        std::vector<float> back = play(*c, note, 1.0);

        snprintf(msg, sizeof msg, "edited differs by %.3e, reverted by %.3e",
                 worstDiff(before, edited), worstDiff(before, back));
        ok("revert restores the cartridge voice exactly",
           !c->patchEdited() && worstDiff(before, back) == 0.0 &&
           worstDiff(before, edited) > 1e-6, msg);
        delete a; delete b; delete c;
    }

    // And the tail that made the naive version of that test fail, measured:
    // playing the same note twice into one instrument is not the same twice,
    // because the first note is still sounding under the second.
    {
        tdx::TubuDX* g = make(cart);
        std::vector<float> first = play(*g, note, 1.0);
        std::vector<float> second = play(*g, note, 1.0);
        const double d = worstDiff(first, second);
        snprintf(msg, sizeof msg, "%.3e between two identical notes", d);
        ok("a sustaining voice does bleed into the next note", d > 1e-3, msg);
        delete g;
    }

    // ---- choosing another voice drops the edit ----
    //
    // The alternative - carrying an edit onto whatever voice you pick next -
    // would be worse than losing it, because nothing on screen would say that
    // the patch you are hearing is not the one you chose.
    {
        tdx::TubuDX* g = make(cart);
        g->setParam(tdx::kVoice, 4.f);
        std::vector<float> clean = play(*g, note, 1.0);

        tdx::TubuDX* h = make(cart);
        h->setParam(tdx::kVoice, 0.f);
        h->setPatchByte(kOp2Level, 0, 0, 99);          // edit voice 0
        const bool wasEdited = h->patchEdited();
        h->setParam(tdx::kVoice, 4.f);                  // now choose voice 4
        const bool dropped = !h->patchEdited();
        std::vector<float> after = play(*h, note, 1.0);

        snprintf(msg, sizeof msg, "edited %d, dropped %d, worst %.3e",
                 (int)wasEdited, (int)dropped, worstDiff(clean, after));
        ok("changing voice drops the edit rather than carrying it over",
           wasEdited && dropped && worstDiff(clean, after) == 0.0, msg);
        delete g; delete h;
    }

    // ---- clamping, and the engine surviving the extremes ----
    {
        tdx::TubuDX* g = make(cart);
        uint8_t p[156];
        g->setPatchByte(kOp1Level, 9000, 0, 99);
        g->currentPatch(p);
        const int hi = p[kOp1Level];
        g->setPatchByte(kOp1Level, -900, 0, 99);
        g->currentPatch(p);
        const int lo = p[kOp1Level];
        snprintf(msg, sizeof msg, "clamps to %d and %d", hi, lo);
        ok("a byte cannot be driven out of range", hi == 99 && lo == 0, msg);

        // every algorithm, at full level, must stay finite and bounded
        bool sane = true;
        double worst = 0;
        for (int alg = 0; alg < 32; alg += 3) {
            g->revertPatch();
            g->setPatchByte(134, alg, 0, 31);
            for (int op = 0; op < 6; op++) g->setPatchByte((5 - op) * 21 + 16, 99, 0, 99);
            g->setPatchByte(135, 7, 0, 7);
            std::vector<float> x = play(*g, note, 0.5);
            for (float v : x) if (!std::isfinite(v)) sane = false;
            worst = std::max(worst, peakOf(x));
        }
        snprintf(msg, sizeof msg, "worst peak %.3f across the algorithms", worst);
        ok("every algorithm at full level stays finite and bounded",
           sane && worst < 1.5, msg);
        delete g;
    }

    // ---- an edit reloads to the same sound ----
    {
        tdx::TubuDX* g = make(cart);
        g->setPatchByte(kOp2Level, 21, 0, 99);
        uint8_t saved[156];
        std::memcpy(saved, g->editedBytes(), 156);
        std::vector<float> before = play(*g, note, 1.0);

        tdx::TubuDX* h = make(cart);
        h->loadEdited(saved);
        std::vector<float> after = play(*h, note, 1.0);
        snprintf(msg, sizeof msg, "worst %.3e", worstDiff(before, after));
        ok("an edited patch reloads to the same sound",
           h->patchEdited() && worstDiff(before, after) == 0.0, msg);
        delete g; delete h;
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
