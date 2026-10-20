// Prev/Next Patch and Prev/Next Favourite, as buttons a controller can press.
//
// Shared by the three plug-ins that load DX7 cartridges. Each decides for
// itself where the voice lands - a pad, the patch, a drum lane - and asks this
// only where to go.

#pragma once

#include "cart_favourites.h"
#include "cart_index.h"
#include "patch_step.h"
#include "platform.h"

#include <string>

namespace mdx {

enum { kPrevPatch, kNextPatch, kPrevFavourite, kNextFavourite, kNumPatchTriggers };

const char* const kPatchTriggerNames[kNumPatchTriggers] = {
    "Prev Patch", "Next Patch", "Prev Favourite", "Next Favourite",
};

// Each plug-in's own starred patches. Per plug-in on purpose: what suits a
// drum machine is not what suits the tubu.
inline std::string favouritesFile(const char* plugin) {
    return mplat::userDataFolder(plugin, "Favourites") + "/favourites.txt";
}

// Moves p to where trigger t lands. idx is the browser's index when an editor
// has one ready; without it the folder is read here, which is why this runs
// off the audio thread.
inline bool patchForTrigger(int t, PatchRef& p, const std::string& cartFolder,
                            const std::string& favFile, const CartIndex* idx = nullptr) {
    const int dir = (t == kPrevPatch || t == kPrevFavourite) ? -1 : +1;
    if (t == kPrevFavourite || t == kNextFavourite) {
        Favourites f;
        f.setFile(favFile);                       // re-read: the editor may have starred one
        return stepFavourite(f.all(), p, dir);
    }
    if (idx && !idx->empty()) return stepPatch(*idx, p, dir);
    CartIndex fresh;
    fresh.scan(cartFolder);
    return stepPatch(fresh, p, dir);
}

inline const char* noPatchMessage(int t) {
    return (t == kPrevFavourite || t == kNextFavourite)
        ? "No favourites yet - star some in the browser."
        : "No cartridges in the folder.";
}

} // namespace mdx
