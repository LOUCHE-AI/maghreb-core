// Writing a pattern down as a standard MIDI file.
//
// Sixty lines rather than a dependency, for the same reason tape_restore.cpp
// has its own FFT: a plug-in that drags in a MIDI library is a plug-in that
// stops loading on a machine which does not have it.
//
// Format 0, one track. That is the whole of what a drum pattern needs, and it
// is the form every host will drop onto a track without asking questions.

#pragma once

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>

namespace mmidi {

// Ticks rather than seconds: a pattern is written in musical time, so it
// follows whatever tempo the host is on when it is played back.
struct Note {
    int32_t tick = 0;
    int32_t lengthTicks = 24;
    int note = 60;
    int velocity = 100;
    int channel = 9;          // the drum channel, counted from zero
};

namespace detail {

inline void putVar(std::vector<uint8_t>& out, uint32_t v) {
    uint8_t buf[4];
    int n = 0;
    buf[n++] = (uint8_t)(v & 0x7F);
    while ((v >>= 7) > 0) buf[n++] = (uint8_t)((v & 0x7F) | 0x80);
    while (n > 0) out.push_back(buf[--n]);
}

inline void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24)); out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));  out.push_back((uint8_t)v);
}

inline void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back((uint8_t)(v >> 8)); out.push_back((uint8_t)v);
}

} // namespace detail

// `bpm` is written as the file's tempo, so the pattern arrives at the speed it
// was made at even in a project set to something else.
inline bool writeFile(const std::string& path, const std::vector<Note>& notes,
                      int ppq, double bpm, const std::string& trackName) {
    if (ppq <= 0) return false;

    // One event per note end and one per note start, then sorted: note offs
    // have to be interleaved with the starts that follow them.
    struct Ev { int32_t tick; int order; uint8_t status, d1, d2; };
    std::vector<Ev> evs;
    evs.reserve(notes.size() * 2);
    for (const Note& n : notes) {
        const uint8_t ch = (uint8_t)std::max(0, std::min(15, n.channel));
        const uint8_t key = (uint8_t)std::max(0, std::min(127, n.note));
        const uint8_t vel = (uint8_t)std::max(1, std::min(127, n.velocity));
        evs.push_back({std::max(0, n.tick), 1, (uint8_t)(0x90 | ch), key, vel});
        evs.push_back({std::max(0, n.tick) + std::max(1, n.lengthTicks), 0,
                       (uint8_t)(0x80 | ch), key, 0});
    }
    // Ends before starts at the same tick, so a repeated note retriggers
    // rather than being cut by its own predecessor's release.
    std::sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    std::vector<uint8_t> track;
    detail::putVar(track, 0);
    track.push_back(0xFF); track.push_back(0x03);
    const std::string name = trackName.substr(0, 127);
    detail::putVar(track, (uint32_t)name.size());
    track.insert(track.end(), name.begin(), name.end());

    const uint32_t usPerQuarter = (uint32_t)(60000000.0 / std::max(1.0, bpm));
    detail::putVar(track, 0);
    track.push_back(0xFF); track.push_back(0x51); track.push_back(0x03);
    track.push_back((uint8_t)(usPerQuarter >> 16));
    track.push_back((uint8_t)(usPerQuarter >> 8));
    track.push_back((uint8_t)usPerQuarter);

    int32_t last = 0;
    for (const Ev& e : evs) {
        detail::putVar(track, (uint32_t)(e.tick - last));
        last = e.tick;
        track.push_back(e.status); track.push_back(e.d1); track.push_back(e.d2);
    }
    detail::putVar(track, 0);
    track.push_back(0xFF); track.push_back(0x2F); track.push_back(0x00);

    std::vector<uint8_t> out;
    const char* mthd = "MThd";
    out.insert(out.end(), mthd, mthd + 4);
    detail::put32(out, 6);
    detail::put16(out, 0);                       // format 0
    detail::put16(out, 1);                       // one track
    detail::put16(out, (uint16_t)ppq);
    const char* mtrk = "MTrk";
    out.insert(out.end(), mtrk, mtrk + 4);
    detail::put32(out, (uint32_t)track.size());
    out.insert(out.end(), track.begin(), track.end());

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t wrote = std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return wrote == out.size();
}

} // namespace mmidi
