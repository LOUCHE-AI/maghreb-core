// An index of every DX7 cartridge on the machine.
//
// Browsing used to mean a native file dialog - which shows file names and
// nothing else - followed by stepping through thirty-two numbered voices one
// button press at a time. With thirty-four cartridges installed that is 1088
// patches reachable only by counting, and no way to find "the one called
// BELLS" short of opening every file.
//
// So the folder is read once, up front, and every voice name comes with it.
// Thirty-four cartridges is 136 KB and a few milliseconds, which buys a list
// you can look at and a search you can type into.
//
// Framework-free, so it can be tested offline. Shared by Maghreb DX, Tubu DX
// and Maghreb Seq; each supplies its own dx_sysex.h on the include path.

#pragma once
#include "dx_sysex.h"
#include "cart_type.h"

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace mdx {

struct CartEntry {
    // One per voice, worked out at scan time while the patch bytes are still
    // in hand: reading them again later to answer "is this a bass" would mean
    // opening every file in the folder again.
    std::vector<uint8_t> types;
    std::string path;
    std::string name;                  // the file stem, which is what a player calls it
    std::vector<std::string> voices;   // thirty-two, in cartridge order
};

// One patch, as a place in the index rather than as a copy of its bytes.
struct CartHit {
    int cart = -1;
    int voice = -1;
};

inline std::string lowered(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::tolower((unsigned char)c);
    return o;
}

class CartIndex {
public:
    // Reads every .syx under the folder. Anything that is not a 32-voice bulk
    // dump is skipped rather than reported: the folder is the user's, and a
    // stray firmware file being quietly ignored is better than an error box.
    //
    // Recursive, and it follows symlinks. A cartridge library is almost never
    // flat - the SynprezFM set alone is thirty-odd files in a folder of its
    // own - and following links means this folder can point at a collection
    // that already exists somewhere else rather than holding a second copy of
    // it. Depth is capped because a link that points at one of its own parents
    // is otherwise an infinite walk.
    void scan(const std::string& folder) {
        namespace fs = std::filesystem;
        carts_.clear();
        folder_ = folder;
        std::error_code ec;
        if (!fs::exists(folder, ec)) return;

        constexpr int kMaxDepth = 4;
        const auto opts = fs::directory_options::follow_directory_symlink |
                          fs::directory_options::skip_permission_denied;

        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(folder, opts, ec), end;
             it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (it.depth() >= kMaxDepth) it.disable_recursion_pending();
            if (!it->is_regular_file(ec)) continue;
            if (lowered(it->path().extension().string()) != ".syx") continue;
            files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());

        for (const auto& f : files) {
            std::vector<SysexVoice> v;
            if (loadSysex(f.string().c_str(), v) <= 0) continue;
            CartEntry ce;
            ce.path = f.string();
            // Lexical, not filesystem::relative: the latter resolves symlinks,
            // so a folder that links out to an existing library would name
            // every cartridge "../../wherever/it/really/lives".
            auto r = f.lexically_relative(folder);
            ce.name = r.empty() ? f.stem().string()
                                : r.replace_extension().string();
            ce.voices.reserve(v.size());
            ce.types.reserve(v.size());
            for (const auto& s : v) {
                std::string n = s.name;
                // an unnamed voice is still a voice; give it something to click
                if (n.empty()) n = "(unnamed)";
                ce.types.push_back((uint8_t)classify(n.c_str(), s.data));
                ce.voices.push_back(n);
            }
            carts_.push_back(std::move(ce));
        }
    }

    // The kind of sound a voice is, for the browser's filter. Sustained when
    // the index predates types, which is the least surprising thing to be.
    VoiceType typeOf(int cart, int voice) const {
        if (cart < 0 || cart >= (int)carts_.size()) return VoiceType::Sustained;
        const CartEntry& c = carts_[(size_t)cart];
        if (voice < 0 || voice >= (int)c.types.size()) return VoiceType::Sustained;
        return (VoiceType)c.types[(size_t)voice];
    }

    const std::string& folder() const { return folder_; }
    int count() const { return (int)carts_.size(); }
    bool empty() const { return carts_.empty(); }
    const CartEntry& cart(int i) const { return carts_[i]; }

    int voiceCount(int cart) const {
        return (cart >= 0 && cart < count()) ? (int)carts_[cart].voices.size() : 0;
    }

    int indexOfPath(const std::string& path) const {
        for (int i = 0; i < count(); i++) if (carts_[i].path == path) return i;
        return -1;
    }

    // Case-insensitive substring, over voice names and cartridge names both,
    // so "rom1" finds a cartridge and "bells" finds a patch inside one. Kept
    // in cartridge order, because that is the order the list is drawn in and
    // a result jumping around while you type is worse than no ranking at all.
    std::vector<CartHit> find(const std::string& query, int limit = 512) const {
        std::vector<CartHit> out;
        const std::string q = lowered(query);
        if (q.empty()) return out;
        for (int c = 0; c < count() && (int)out.size() < limit; c++) {
            const bool cartMatches = lowered(carts_[c].name).find(q) != std::string::npos;
            for (int v = 0; v < (int)carts_[c].voices.size(); v++) {
                if ((int)out.size() >= limit) break;
                if (cartMatches ||
                    lowered(carts_[c].voices[v]).find(q) != std::string::npos)
                    out.push_back({c, v});
            }
        }
        return out;
    }

    // "ROM1A - BRASS   1", for a result row that has to say where it came from.
    std::string label(const CartHit& h) const {
        if (h.cart < 0 || h.cart >= count()) return "";
        const CartEntry& c = carts_[h.cart];
        if (h.voice < 0 || h.voice >= (int)c.voices.size()) return c.name;
        return c.name + "  -  " + c.voices[h.voice];
    }

private:
    std::vector<CartEntry> carts_;
    std::string folder_;
};

} // namespace mdx
