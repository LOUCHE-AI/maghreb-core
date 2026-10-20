// Does picking a patch in the browser actually give you that patch?
//
// The browser itself is two lists and a search box - drawing it is not the
// risky part. The risky part is the wiring underneath: the list hands back a
// cartridge path and a voice number, and something has to turn that into the
// right 156 bytes reaching the engine. That is what this measures, on the
// cartridges actually installed on this machine rather than on a fixture.

#include "cart_index.h"
#include "tubudx_dsp.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static const double SR = 44100.0;

// One note through the instrument exactly as the plug-in would play it.
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
    double p = 0;
    for (float x : v) p = std::max(p, (double)std::fabs(x));
    return p;
}
static double corr(const std::vector<float>& a, const std::vector<float>& b) {
    double num = 0, da = 0, db = 0;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) { num += a[i]*b[i]; da += a[i]*a[i]; db += b[i]*b[i]; }
    return num / std::max(1e-12, std::sqrt(da * db));
}

// what the editor's pick callback does, minus the drawing
static bool pick(tdx::TubuDX& g, const mdx::CartIndex& ix, int cart, int voice) {
    if (cart < 0 || cart >= ix.count()) return false;
    if (g.loadCartridge(ix.cart(cart).path.c_str()) <= 0) return false;
    g.setParam(tdx::kVoice, (float)voice);
    return true;
}

int main() {
    const char* base = std::getenv("LOCALAPPDATA");
    const std::string dir = std::string(base ? base : ".") + "\\MaghrebDX\\Cartridges";

    mdx::CartIndex ix;
    ix.scan(dir);
    char msg[400];

    snprintf(msg, sizeof msg, "%d cartridges, %d patches", ix.count(), ix.count() * 32);
    ok("the browser has something to show", ix.count() > 0, msg);
    if (ix.empty()) { printf("\nFAILURES\n"); return 1; }

    tdx::TubuDX g;
    g.setSampleRate(SR);

    // ---- the name in the list is the name the engine reports ----
    {
        bool allMatch = true;
        std::string bad;
        int checked = 0;
        for (int c = 0; c < ix.count(); c += 5) {
            if (!pick(g, ix, c, 0)) { allMatch = false; bad = "load failed"; break; }
            for (int v = 0; v < 32; v += 7) {
                g.setParam(tdx::kVoice, (float)v);
                checked++;
                const std::string engineName = g.voiceName(v);
                const std::string listName = ix.cart(c).voices[v];
                if (engineName != listName) {
                    allMatch = false;
                    if (bad.empty())
                        bad = "list \"" + listName + "\" vs engine \"" + engineName + "\"";
                }
            }
        }
        snprintf(msg, sizeof msg, "%d checked  %s", checked, bad.c_str());
        ok("what the list says is what the engine loaded", allMatch, msg);
    }

    // ---- picking a different voice really changes the sound ----
    {
        const int note = 60;
        pick(g, ix, 0, 0);
        std::vector<float> a = play(g, note, 1.2);
        pick(g, ix, 0, 1);
        std::vector<float> b = play(g, note, 1.2);
        pick(g, ix, 0, 0);
        std::vector<float> a2 = play(g, note, 1.2);

        snprintf(msg, sizeof msg, "voice0 vs voice1 corr %.3f, voice0 repeat corr %.3f",
                 corr(a, b), corr(a, a2));
        ok("a different voice is a different sound", std::fabs(corr(a, b)) < 0.9, msg);
        ok("and picking the same voice again is repeatable", corr(a, a2) > 0.999, msg);
    }

    // ---- picking a different cartridge really changes the sound ----
    if (ix.count() > 1) {
        pick(g, ix, 0, 0);
        std::vector<float> a = play(g, 60, 1.0);
        pick(g, ix, ix.count() - 1, 0);
        std::vector<float> b = play(g, 60, 1.0);
        snprintf(msg, sizeof msg, "%s vs %s, corr %.3f",
                 ix.cart(0).name.c_str(), ix.cart(ix.count()-1).name.c_str(), corr(a, b));
        ok("a different cartridge is a different sound", std::fabs(corr(a, b)) < 0.95, msg);
    }

    // ---- every patch in the folder is playable, finite and not deafening ----
    //
    // This is the one that matters for actually using the browser: you will
    // click through hundreds of these, and one that is silent, infinite or
    // twice as loud as the rest ruins the browsing.
    {
        int silent = 0, wild = 0, hot = 0, total = 0;
        double worst = 0;
        std::string worstName, silentName;
        for (int c = 0; c < ix.count(); c++) {
            pick(g, ix, c, 0);
            for (int v = 0; v < 32; v++) {
                g.setParam(tdx::kVoice, (float)v);
                std::vector<float> x = play(g, 60, 0.5);
                total++;
                bool finite = true;
                for (float s : x) if (!std::isfinite(s)) finite = false;
                const double pk = peakOf(x);
                if (!finite) { wild++; }
                if (pk < 1e-4) { silent++; if (silentName.empty()) silentName = ix.label({c, v}); }
                if (pk > 1.0) hot++;
                if (pk > worst) { worst = pk; worstName = ix.label({c, v}); }
            }
        }
        snprintf(msg, sizeof msg, "%d patches, %d silent, %d non-finite, %d over 1.0, worst %.2f (%s)",
                 total, silent, wild, hot, worst, worstName.c_str());
        ok("every patch in the folder plays", silent == 0 && wild == 0, msg);
        ok("and none of them clips", hot == 0 && worst <= 1.0, msg);
    }

    // ---- search leads somewhere real ----
    {
        auto hits = ix.find("bass");
        bool played = !hits.empty();
        double pk = 0;
        if (played) {
            const mdx::CartHit& h = hits[hits.size() / 2];
            played = pick(g, ix, h.cart, h.voice);
            if (played) pk = peakOf(play(g, 48, 1.0));
        }
        snprintf(msg, sizeof msg, "%d hits, picked \"%s\", peak %.3f",
                 (int)hits.size(),
                 hits.empty() ? "-" : ix.label(hits[hits.size()/2]).c_str(), pk);
        ok("a search result loads and sounds", played && pk > 1e-3, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
