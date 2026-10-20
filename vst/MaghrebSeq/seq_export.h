// Getting a pattern out of the plug-in as something other than its own audio.
//
// Three routes, one walk of the pattern: MIDI to play anything with, a loop,
// and the strokes as a kit of one shots. They live here rather than in the
// wrapper so they can be run - and checked - without a host: the wrapper's
// versions are three lines each that hand these the folder to write into.

#pragma once

#include "seq_dsp.h"
#include "dx_patch.h"
#include "midi_write.h"
#include "wav_io.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace mseq {
namespace xport {

// A name a person can find again: what was playing, and when it was written.
inline std::string stampedName(const MaghrebSeq& dsp, const char* what,
                               const char* extension) {
    const char* rhythm = "pattern";
    for (int lane = 0; lane < kLanes; lane++)
        if (dsp.laneEnabled(0, lane)) { rhythm = kRhythmNames[dsp.rhythmOf(lane)]; break; }

    char stamp[32];
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    #if defined(_WIN32)
    localtime_s(&tm, &t);
    #else
    localtime_r(&t, &tm);
    #endif
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H%M%S", &tm);

    char name[256];
    snprintf(name, sizeof name, "%s%s%s %s%s", rhythm, *what ? " " : "", what,
             stamp, extension);
    return name;
}

// 96 ticks to the quarter puts a sixteenth on 24 and leaves swing somewhere
// exact to land: two thirds of a step is sixteen ticks, not a rounding.
static const int kPPQ = 96;

inline std::string writeMidi(const MaghrebSeq& dsp, const std::string& folder,
                             std::string* err = nullptr) {
    const std::vector<MaghrebSeq::Hit> hits = dsp.pattern(dsp.cycleSteps());
    if (hits.empty()) {
        if (err) *err = "nothing to write down - every lane is muted or silent";
        return {};
    }

    const double ticksPerStep = kPPQ / 4.0;
    std::vector<mmidi::Note> notes;
    notes.reserve(hits.size());
    for (const MaghrebSeq::Hit& h : hits) {
        mmidi::Note n;
        n.tick = (int32_t)std::llround(((double)h.step + h.lateSteps) * ticksPerStep);
        n.lengthTicks = (int32_t)(ticksPerStep * 0.5);
        // The note each stroke already answers to in Maghreb DX and Tubu DX,
        // so this file plays them without a mapping in between.
        n.note = mdx::kFirstNote + h.stroke;
        n.velocity = 100;
        n.channel = 9;
        notes.push_back(n);
    }

    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::string path =
        (std::filesystem::path(folder) / stampedName(dsp, "", ".mid")).string();
    if (!mmidi::writeFile(path, notes, kPPQ, dsp.tempo(), "Maghreb Seq")) {
        if (err) *err = "could not write " + path;
        return {};
    }
    return path;
}

inline std::string writeLoop(MaghrebSeq& dsp, const std::string& folder,
                             int sampleRate, std::string* err = nullptr) {
    std::vector<float> L, R;
    if (!dsp.renderPattern(L, R, dsp.cycleSteps(), dsp.tempo())) {
        if (err) *err = "nothing to render - every lane is muted or silent";
        return {};
    }

    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::string path =
        (std::filesystem::path(folder) / stampedName(dsp, "loop", ".wav")).string();

    std::vector<std::vector<float>> ch = {L, R};
    if (!mwav::writeWav24(path, ch, sampleRate)) {
        if (err) *err = "could not write " + path;
        return {};
    }
    return path;
}

// Each stroke on its own, into a folder of their own. Returns how many.
inline int writeKit(MaghrebSeq& dsp, const std::string& parentFolder,
                    int sampleRate, std::string* err = nullptr) {
    // The lane whose settings the strokes are struck with: the first one
    // playing, so a kit taken from a groove sounds like that groove.
    int lane = 0;
    for (int i = 0; i < kLanes; i++)
        if (dsp.laneEnabled(0, i)) { lane = i; break; }

    const std::filesystem::path folder =
        std::filesystem::path(parentFolder) / stampedName(dsp, "kit", "");
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);

    int written = 0;
    for (int stroke = 0; stroke < kNumStrokes; stroke++) {
        std::vector<float> L, R;
        if (!dsp.renderStroke(L, R, lane, stroke, 2.0)) continue;

        // Trimmed where it died away, so a sampler is not asked to hold two
        // seconds of nothing per pad.
        size_t last = 0;
        for (size_t i = 0; i < L.size(); i++)
            if (std::fabs(L[i]) > 1e-4f || std::fabs(R[i]) > 1e-4f) last = i;
        const size_t keep = std::min(L.size(), last + (size_t)(0.05 * sampleRate));
        if (keep < 16) continue;
        L.resize(keep);
        R.resize(keep);

        // Named by the note it answers to first, so a sampler that sorts by
        // filename lays the kit out in the order the pads are in.
        char name[128];
        snprintf(name, sizeof name, "%d %s.wav", mdx::kFirstNote + stroke,
                 kStrokeNames[stroke]);
        std::vector<std::vector<float>> ch = {L, R};
        if (mwav::writeWav24((folder / name).string(), ch, sampleRate)) written++;
    }

    if (written == 0 && err) *err = "could not write into " + folder.string();
    return written;
}

} // namespace xport
} // namespace mseq
