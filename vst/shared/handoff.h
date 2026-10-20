// Where TapeRepair leaves a mended tape, and how TapeGrain finds it.
//
// The two plug-ins never talk to each other. They agree on a folder and on a
// name that says where the file came from, and that is enough for one to hand
// the other a side of tape without either knowing the other is running - or
// being open at the time, which matters when the repair happened yesterday.
//
// The folder is TapeGrain's own samples folder rather than somewhere private
// to the pair, so a handoff is an ordinary file in an ordinary place: findable
// through the file dialog, or the file manager, by someone who has never heard
// of this convention.
//
// Kept here rather than in both plug-ins so it can only be changed once.

#pragma once

#include "platform.h"

#include <filesystem>
#include <string>

namespace mhand {

inline std::string folder() { return mplat::userDataFolder("TapeGrain", "Samples"); }

// What TapeRepair names its files, and therefore what TapeGrain looks for.
inline const char* prefix() { return "TapeRepair "; }

// The most recent handoff, or empty if there has never been one.
//
// By modification time rather than by the timestamp in the name: the name is
// for a person to read, and a file that was copied or restored should still
// count as the newest one.
inline std::string newestIn(const std::string& where) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir(where);
    if (!fs::is_directory(dir, ec)) return {};

    fs::path best;
    fs::file_time_type bestTime{};
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path& path = it->path();
        const std::string name = path.filename().string();
        if (name.rfind(prefix(), 0) != 0) continue;
        std::string ext = path.extension().string();
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".wav") continue;

        std::error_code te;
        const fs::file_time_type t = fs::last_write_time(path, te);
        if (te) continue;
        if (best.empty() || t > bestTime) { best = path; bestTime = t; }
    }
    return best.string();
}

inline std::string newest() { return newestIn(folder()); }

// Just the file name, for saying which one was loaded.
inline std::string nameOf(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

} // namespace mhand
