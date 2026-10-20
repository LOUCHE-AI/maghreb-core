// Finding a set of separated stems from any one of its files.
//
// Live 12.3+ writes stem separation results into
//   <Project>/Samples/Processed/Stems/<clip>/<clip> [timestamp] (Vocals).wav
// with the four parts differing only by the suffix in brackets. That is
// predictable enough to fill all four slots from a single file pick, which
// turns loading a separated track from four trips through a file dialog into
// one.
//
// Demucs writes the same four parts, but as whole filenames inside a folder of
// their own:
//   <out>/<model>/<track>/vocals.wav   drums.wav   bass.wav   other.wav
// Both layouts are recognised. Which one produced the files does not matter to
// anything downstream - the slots are filled in the same order either way.
//
// Framework-free on purpose, so the name matching can be tested against real
// filenames offline rather than by clicking through a host.

#pragma once
#include <string>
#include <filesystem>
#include <cctype>

namespace tg {

// Slot order: A B C D. Vocals first because it is the part most often played
// alone; Other last because it is the leftovers.
static const char* const kStemSuffixes[kStemSlots] = {
    "Vocals", "Drums", "Bass", "Other"
};

inline bool iequals(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return i == a.size() && !b[i];
}

// Live's layout: "<base> (Vocals).wav", the part in brackets at the end.
inline int findBracketed(const std::string& dir, const std::string& stem,
                         const std::string& ext, std::string out[kStemSlots]) {
    const size_t open = stem.find_last_of('(');
    if (open == std::string::npos || stem.back() != ')') return 0;
    const std::string suffix = stem.substr(open + 1, stem.size() - open - 2);

    bool known = false;
    for (int i = 0; i < kStemSlots; i++) if (iequals(suffix, kStemSuffixes[i])) known = true;
    if (!known) return 0;

    const std::string base = stem.substr(0, open);      // keeps its trailing space

    int found = 0;
    std::error_code ec;
    for (int i = 0; i < kStemSlots; i++) {
        std::string candidate = dir + base + "(" + kStemSuffixes[i] + ")" + ext;
        if (std::filesystem::exists(candidate, ec)) { out[i] = candidate; found++; }
    }
    return found;
}

// Demucs's layout: the whole filename is the part, so "vocals.wav" beside
// "drums.wav". Demucs writes them lower case; the capitalised spelling is
// tried too rather than assuming a tool never changes its mind about that.
inline int findNamedParts(const std::string& dir, const std::string& stem,
                          const std::string& ext, std::string out[kStemSlots]) {
    bool known = false;
    for (int i = 0; i < kStemSlots; i++) if (iequals(stem, kStemSuffixes[i])) known = true;
    if (!known) return 0;

    int found = 0;
    std::error_code ec;
    for (int i = 0; i < kStemSlots; i++) {
        std::string lower = kStemSuffixes[i];
        for (char& c : lower) c = (char)std::tolower((unsigned char)c);
        for (const std::string& name : {lower, std::string(kStemSuffixes[i])}) {
            std::string candidate = dir + name + ext;
            if (std::filesystem::exists(candidate, ec)) { out[i] = candidate; found++; break; }
        }
    }
    return found;
}

// Given one file from a separated set, fills `out` with the path of each part
// that exists on disk, in slot order, and returns how many were found.
// Returns 0 if the path does not look like either layout - the caller should
// then treat it as an ordinary sample.
inline int findStemSet(const std::string& picked, std::string out[kStemSlots]) {
    for (int i = 0; i < kStemSlots; i++) out[i].clear();
    if (picked.empty()) return 0;

    const size_t slash = picked.find_last_of("/\\");
    const std::string dir  = (slash == std::string::npos) ? std::string()
                                                          : picked.substr(0, slash + 1);
    const std::string file = (slash == std::string::npos) ? picked
                                                          : picked.substr(slash + 1);

    const size_t dot = file.find_last_of('.');
    if (dot == std::string::npos) return 0;
    const std::string ext  = file.substr(dot);          // ".wav"
    const std::string stem = file.substr(0, dot);

    if (const int n = findBracketed(dir, stem, ext, out)) return n;
    if (const int n = findNamedParts(dir, stem, ext, out)) return n;
    for (int i = 0; i < kStemSlots; i++) out[i].clear();
    return 0;
}

} // namespace tg
