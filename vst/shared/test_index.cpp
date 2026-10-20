// The cartridge index, measured against the folder actually on this machine.

#include "cart_index.h"
#include "platform.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

int main() {
    // Where the plug-ins themselves look, rather than a hand-built Windows
    // path: this used to be %LOCALAPPDATA%\MaghrebDX\Cartridges spelled out
    // with backslashes, which on Linux resolved to a folder that cannot exist,
    // so every check below failed for the one reason that had nothing to do
    // with the index.
    const std::string dir = mplat::userDataFolder("MaghrebDX", "Cartridges");

    mdx::CartIndex ix;
    ix.scan(dir);
    char msg[400];

    snprintf(msg, sizeof msg, "%d cartridges", ix.count());
    ok("the folder indexes", ix.count() > 0, msg);

    int voices = 0, named = 0, blank = 0;
    for (int c = 0; c < ix.count(); c++)
        for (const auto& v : ix.cart(c).voices) {
            voices++;
            if (v == "(unnamed)") blank++; else named++;
        }
    snprintf(msg, sizeof msg, "%d voices, %d named, %d blank", voices, named, blank);
    ok("every cartridge yields thirty-two voices", voices == ix.count() * 32, msg);

    // sorted, so the list does not reshuffle between runs
    bool sorted = true;
    for (int c = 1; c < ix.count(); c++)
        if (ix.cart(c - 1).path > ix.cart(c).path) sorted = false;
    ok("the list is in a stable order", sorted);

    // A name from the DX7 factory ROM, to prove the unpack lines up. Only a
    // machine that has rom1a.syx can say anything about it, so a collection
    // without it is not a failure - it is a check that does not apply.
    int r1a = -1;
    for (int c = 0; c < ix.count() && r1a < 0; c++)
        if (mdx::lowered(ix.cart(c).path).find("rom1a.syx") != std::string::npos) r1a = c;
    if (r1a < 0)
        printf("SKIP   rom1a is not in this collection, so the factory names go unchecked\n");
    if (r1a >= 0) {
        const auto& v = ix.cart(r1a).voices;
        snprintf(msg, sizeof msg, "voice 1 is \"%s\", voice 7 is \"%s\"",
                 v[0].c_str(), v[6].c_str());
        ok("and carries the factory names", v[0] == "BRASS   1" && v[6] == "ORCHESTRA", msg);
    }

    // search
    auto bass = ix.find("bass");
    snprintf(msg, sizeof msg, "%d hits", (int)bass.size());
    ok("searching finds patches by name", bass.size() > 5, msg);

    bool allMatch = true;
    for (const auto& h : bass) {
        const std::string lv = mdx::lowered(ix.cart(h.cart).voices[h.voice]);
        const std::string lc = mdx::lowered(ix.cart(h.cart).name);
        if (lv.find("bass") == std::string::npos && lc.find("bass") == std::string::npos)
            allMatch = false;
    }
    ok("and every hit really contains the term", allMatch);

    auto upper = ix.find("BASS");
    snprintf(msg, sizeof msg, "%d vs %d", (int)bass.size(), (int)upper.size());
    ok("search ignores case", bass.size() == upper.size() && !upper.empty(), msg);

    // Searching a cartridge's own name reaches every voice in it, whatever
    // that cartridge happens to be called on this machine.
    if (ix.count() > 0) {
        const std::string first = ix.cart(0).name;
        auto byCart = ix.find(first);
        snprintf(msg, sizeof msg, "\"%s\" gives %d hits", first.c_str(), (int)byCart.size());
        ok("a cartridge name matches all of its voices", byCart.size() >= 32, msg);
    }

    ok("an empty query finds nothing", ix.find("").empty());
    ok("a term that is not there finds nothing",
       ix.find("zzzznotapatchzzz").empty());

    const int lim = (int)ix.find("a", 40).size();
    snprintf(msg, sizeof msg, "%d returned", lim);
    ok("the result limit is honoured", lim <= 40, msg);

    if (!bass.empty()) {
        const std::string l = ix.label(bass[0]);
        snprintf(msg, sizeof msg, "\"%s\"", l.c_str());
        ok("a result says which cartridge it came from",
           l.find("  -  ") != std::string::npos, msg);
    }

    // a folder that is not there must not crash or invent anything
    mdx::CartIndex none;
    none.scan("C:\\definitely\\not\\a\\folder\\here");
    ok("a missing folder yields an empty index", none.count() == 0 && none.empty());

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
