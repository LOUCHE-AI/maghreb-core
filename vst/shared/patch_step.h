// Where Prev/Next Patch and Prev/Next Favourite land, from the patch playing
// now.
//
// Patch steps through every voice of every cartridge, in the browser's order,
// and runs on into the next cartridge at either end of one. Favourite steps
// through the plug-in's own starred list in the order things were starred.
// Neither looks at the browser's search, kind or FAVS filters: those belong to
// a window that may not be open, and a button on a controller should do the
// same thing whether it is or not.

#pragma once

#include <string>
#include <vector>

namespace mdx {

struct PatchRef {
    std::string path;
    int voice = 0;
};

// Index is a CartIndex, or anything with count(), voiceCount(i), indexOfPath()
// and cart(i).path - which is what lets the check below use a fake one.
template <class Index>
bool stepPatch(const Index& idx, PatchRef& p, int dir) {
    const int n = idx.count();
    if (n <= 0) return false;
    int c = idx.indexOfPath(p.path);
    int v = p.voice + dir;
    if (c < 0) {                                  // nothing loaded yet: start at an end
        c = dir > 0 ? 0 : n - 1;
        v = dir > 0 ? 0 : idx.voiceCount(c) - 1;
    }
    // Into the next cartridge with anything in it; a broken file reads as
    // empty, and one lap of the folder is as far as this looks.
    for (int tries = 0; v < 0 || v >= idx.voiceCount(c); tries++) {
        if (tries > n) return false;
        c = ((c + dir) % n + n) % n;
        v = dir > 0 ? 0 : idx.voiceCount(c) - 1;
    }
    p.path = idx.cart(c).path;
    p.voice = v;
    return true;
}

// Items are (cartridge path, voice), as Favourites::all() returns them.
inline bool stepFavourite(const std::vector<std::pair<std::string, int>>& items,
                          PatchRef& p, int dir) {
    const int n = (int)items.size();
    if (n == 0) return false;
    int at = -1;
    for (int i = 0; i < n; i++)
        if (items[i].first == p.path && items[i].second == p.voice) { at = i; break; }
    at = at < 0 ? (dir > 0 ? 0 : n - 1) : ((at + dir) % n + n) % n;
    p.path = items[at].first;
    p.voice = items[at].second;
    return true;
}

} // namespace mdx
