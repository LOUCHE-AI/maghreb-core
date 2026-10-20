// A pool of patches worth coming back to, kept per plug-in.
//
// A thousand voices is more than anyone browses twice, and the ones that suit
// a drum machine are not the ones that suit the ṭubūʿ instrument - so each
// plug-in keeps its own list rather than sharing one. It is a plain text file
// next to that plug-in's other data: a voice number and a cartridge path per
// line, so it survives a reinstall and can be read without this code.

#pragma once

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace mdx {

class Favourites {
public:
    using Item = std::pair<std::string, int>;    // cartridge path, voice

    // Loads whatever is already there. Safe on a path that does not exist yet.
    void setFile(const std::string& path) {
        file_ = path;
        items_.clear();
        std::ifstream in(file_);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            const int voice = std::atoi(line.substr(0, tab).c_str());
            std::string cart = line.substr(tab + 1);
            if (!cart.empty()) items_.emplace_back(std::move(cart), voice);
        }
    }

    bool contains(const std::string& cart, int voice) const {
        return find(cart, voice) != items_.end();
    }

    // Adds it if it is not there, removes it if it is. Written out at once:
    // this is a list a person edits a few times an hour, not a hot path.
    void toggle(const std::string& cart, int voice) {
        if (cart.empty()) return;
        auto it = find(cart, voice);
        if (it != items_.end()) items_.erase(it);
        else items_.emplace_back(cart, voice);
        save();
    }

    const std::vector<Item>& all() const { return items_; }
    bool empty() const { return items_.empty(); }

private:
    std::vector<Item>::const_iterator find(const std::string& cart, int voice) const {
        return std::find_if(items_.begin(), items_.end(), [&](const Item& i) {
            return i.second == voice && i.first == cart;
        });
    }
    std::vector<Item>::iterator find(const std::string& cart, int voice) {
        return std::find_if(items_.begin(), items_.end(), [&](const Item& i) {
            return i.second == voice && i.first == cart;
        });
    }

    void save() const {
        if (file_.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(file_).parent_path(), ec);
        std::ofstream out(file_, std::ios::trunc);
        if (!out) return;
        for (const Item& i : items_) out << i.second << '\t' << i.first << '\n';
    }

    std::string file_;
    std::vector<Item> items_;
};

} // namespace mdx
