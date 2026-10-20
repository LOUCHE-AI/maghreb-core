// Prev/Next Patch, Prev/Next Favourite, and the trigger edges that press them.
//
//   g++ -O2 -std=c++17 -I. test_patch_step.cpp -o /tmp/t && /tmp/t

#include "patch_step.h"
#include "triggers.h"

#include <cstdio>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* what, bool c) {
    printf("%s  %s\n", c ? "PASS " : "FAIL ", what);
    if (!c) fails++;
}

// Three cartridges, the middle one unreadable.
struct FakeIndex {
    struct Cart { std::string path; int voices; };
    std::vector<Cart> carts {{"a.syx", 32}, {"broken.syx", 0}, {"c.syx", 32}};
    int count() const { return (int)carts.size(); }
    int voiceCount(int i) const { return carts[i].voices; }
    const Cart& cart(int i) const { return carts[i]; }
    int indexOfPath(const std::string& p) const {
        for (int i = 0; i < count(); i++) if (carts[i].path == p) return i;
        return -1;
    }
};

int main() {
    FakeIndex idx;
    mdx::PatchRef p;

    ok("nothing loaded: Next starts at the first voice",
       mdx::stepPatch(idx, p, +1) && p.path == "a.syx" && p.voice == 0);
    p = {"", 0};
    ok("nothing loaded: Prev starts at the very last voice",
       mdx::stepPatch(idx, p, -1) && p.path == "c.syx" && p.voice == 31);

    p = {"a.syx", 5};
    ok("Next is the next voice", mdx::stepPatch(idx, p, +1) && p.voice == 6);
    p = {"a.syx", 31};
    ok("past the end of a cartridge it runs on, over an empty one",
       mdx::stepPatch(idx, p, +1) && p.path == "c.syx" && p.voice == 0);
    p = {"c.syx", 0};
    ok("and back again", mdx::stepPatch(idx, p, -1) && p.path == "a.syx" && p.voice == 31);
    p = {"c.syx", 31};
    ok("the folder wraps", mdx::stepPatch(idx, p, +1) && p.path == "a.syx" && p.voice == 0);

    FakeIndex none; none.carts = {{"x.syx", 0}};
    p = {"", 0};
    ok("a folder of unreadable cartridges goes nowhere", !mdx::stepPatch(none, p, +1));

    std::vector<std::pair<std::string, int>> favs {{"c.syx", 3}, {"a.syx", 9}};
    p = {"a.syx", 0};
    ok("not a favourite: Next goes to the first one",
       mdx::stepFavourite(favs, p, +1) && p.path == "c.syx" && p.voice == 3);
    ok("then the next", mdx::stepFavourite(favs, p, +1) && p.path == "a.syx" && p.voice == 9);
    ok("and wraps", mdx::stepFavourite(favs, p, +1) && p.path == "c.syx" && p.voice == 3);
    ok("Prev wraps the other way",
       mdx::stepFavourite(favs, p, -1) && p.path == "a.syx" && p.voice == 9);
    ok("no favourites goes nowhere", !mdx::stepFavourite({}, p, +1));

    mplat::Triggers t;
    std::vector<int> ran;
    auto run = [&](int i) { ran.push_back(i); };
    t.feed(2, 1.0); t.feed(2, 0.0);               // a pad, pressed and let go
    t.drain(run);
    ok("a press and release is one press", ran.size() == 1 && ran[0] == 2);
    ran.clear();
    t.feed(2, 1.0); t.feed(2, 1.0);               // held, host resends the value
    t.drain(run);
    ok("holding it does not press again", ran.size() == 1);
    ran.clear();
    t.feed(3, 0.3); t.drain(run);
    ok("below halfway is not a press", ran.empty());

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
