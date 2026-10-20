// What comes out of the plug-in that is not sound.
//
// The claim is narrow: the MIDI file holds the pattern the sequencer plays,
// on the notes the rest of the family answers to. So the file is written and
// then read back with a parser that knows nothing about how it was made.

#include "seq_dsp.h"
#include "midi_write.h"
#include "seq_export.h"
#include "dx_patch.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& d = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, d.empty() ? "" : "  ", d.c_str());
    if (!c) fails++;
}

// ---- a standard MIDI file reader, so the writer is checked against the
// format rather than against itself ----
struct ReadNote { int32_t tick; int note; int velocity; };

struct Parsed {
    int ppq = 0;
    double bpm = 0;
    std::vector<ReadNote> notes;
    bool ok = false;
};

static uint32_t be32(const uint8_t* p) { return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0]<<8)|p[1]); }

static Parsed parseMidi(const std::string& path) {
    Parsed out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    std::vector<uint8_t> b;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f);
    if (b.size() < 22 || memcmp(b.data(), "MThd", 4) != 0) return out;

    out.ppq = be16(&b[12]);
    size_t p = 8 + be32(&b[4]);
    if (p + 8 > b.size() || memcmp(&b[p], "MTrk", 4) != 0) return out;
    const uint32_t len = be32(&b[p + 4]);
    p += 8;
    const size_t end = std::min(b.size(), p + len);

    int32_t tick = 0;
    uint8_t running = 0;
    while (p < end) {
        uint32_t delta = 0;
        while (p < end) { delta = (delta << 7) | (b[p] & 0x7F); if (!(b[p++] & 0x80)) break; }
        tick += (int32_t)delta;
        if (p >= end) break;

        uint8_t status = b[p];
        if (status & 0x80) { running = status; p++; } else { status = running; }

        if (status == 0xFF) {
            const uint8_t type = b[p++];
            uint32_t mlen = 0;
            while (p < end) { mlen = (mlen << 7) | (b[p] & 0x7F); if (!(b[p++] & 0x80)) break; }
            if (type == 0x51 && mlen == 3) {
                const uint32_t us = (b[p] << 16) | (b[p+1] << 8) | b[p+2];
                out.bpm = 60000000.0 / (double)us;
            }
            p += mlen;
            if (type == 0x2F) break;
        } else if ((status & 0xF0) == 0x90 || (status & 0xF0) == 0x80) {
            const int note = b[p], vel = b[p + 1];
            p += 2;
            if ((status & 0xF0) == 0x90 && vel > 0) out.notes.push_back({tick, note, vel});
        } else {
            p += 2;
        }
    }
    out.ok = true;
    return out;
}

int main() {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "seq_export_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    char msg[240];

    mseq::MaghrebSeq seq;
    seq.setSampleRate(48000);
    seq.setTempo(96.0);

    // one lane, a known rhythm, the rest muted
    for (int lane = 1; lane < mseq::kLanes; lane++)
        seq.setParam(mseq::laneParam(lane, mseq::kLaneMute), 1.f);
    seq.setParam(mseq::laneParam(0, mseq::kLaneMute), 0.f);
    seq.setParam(mseq::laneParam(0, mseq::kLaneRhythm), 16.f);   // buhalla, 6/8

    const int64_t steps = seq.cycleSteps();
    const char* pat = mseq::kRhythms[seq.rhythmOf(0)];
    int expected = 0;
    for (int64_t s = 0; s < steps; s++)
        if (pat[s % (int64_t)strlen(pat)] != '.') expected++;

    const std::vector<mseq::MaghrebSeq::Hit> hits = seq.pattern(steps);
    snprintf(msg, sizeof msg, "%zu hits over %lld steps, the rhythm has %d",
             hits.size(), (long long)steps, expected);
    ok("the walk finds exactly the hits the rhythm writes", (int)hits.size() == expected, msg);

    // muting a lane empties it
    seq.setParam(mseq::laneParam(0, mseq::kLaneMute), 1.f);
    ok("a muted lane contributes nothing", seq.pattern(steps).empty());
    seq.setParam(mseq::laneParam(0, mseq::kLaneMute), 0.f);

    // ---- written down, and read back by something that only knows the format
    std::vector<mmidi::Note> notes;
    for (const auto& h : hits) {
        mmidi::Note n;
        n.tick = (int32_t)llround((double)h.step * 24.0);
        n.lengthTicks = 12;
        n.note = mdx::kFirstNote + h.stroke;
        n.velocity = 100;
        notes.push_back(n);
    }
    const std::string path = (dir / "pattern.mid").string();
    ok("a file is written", mmidi::writeFile(path, notes, 96, 96.0, "Maghreb Seq"));

    const Parsed got = parseMidi(path);
    ok("and it parses as a standard MIDI file", got.ok);
    snprintf(msg, sizeof msg, "%d", got.ppq);
    ok("with the division it was given", got.ppq == 96, msg);
    snprintf(msg, sizeof msg, "%.1f bpm", got.bpm);
    ok("and the tempo it was made at", std::fabs(got.bpm - 96.0) < 0.5, msg);

    snprintf(msg, sizeof msg, "%zu read, %zu written", got.notes.size(), notes.size());
    ok("every hit survives the round trip", got.notes.size() == notes.size(), msg);

    bool placed = true, mapped = true;
    for (size_t i = 0; i < got.notes.size() && i < notes.size(); i++) {
        if (got.notes[i].tick != notes[i].tick) placed = false;
        if (got.notes[i].note != notes[i].note) mapped = false;
    }
    ok("each on the sixteenth it was played on", placed);
    snprintf(msg, sizeof msg, "first note is %d, the family starts at %d",
             got.notes.empty() ? -1 : got.notes[0].note, mdx::kFirstNote);
    ok("and on the note the rest of the family answers to", mapped, msg);

    // ---- swing is written down too, not just heard ----
    //
    // On a rhythm that has something on the offbeats: buhalla above is
    // "D.....T...T.", every hit on an even sixteenth, and swing has nothing to
    // hold back. Rhythm 5 has nine offbeat hits.
    seq.setParam(mseq::laneParam(0, mseq::kLaneRhythm), 5.f);
    seq.setParam(mseq::kSwing, 0.f);
    const int64_t swingSteps = seq.cycleSteps();
    const auto straight = seq.pattern(swingSteps);
    seq.setParam(mseq::kSwing, 1.f);
    const auto swung = seq.pattern(swingSteps);
    double moved = 0;
    for (size_t i = 0; i < swung.size() && i < straight.size(); i++)
        moved = std::max(moved, swung[i].lateSteps);
    snprintf(msg, sizeof msg, "odd sixteenths land %.2f of a step late", moved);
    ok("full swing holds the offbeats back", moved > 0.6 && moved < 0.7, msg);

    // ---- rendered, rather than written down ----
    {
        mseq::MaghrebSeq r;
        r.setSampleRate(48000);
        r.setTempo(120.0);
        for (int lane = 1; lane < mseq::kLanes; lane++)
            r.setParam(mseq::laneParam(lane, mseq::kLaneMute), 1.f);
        r.setParam(mseq::laneParam(0, mseq::kLaneRhythm), 5.f);

        const int64_t st = r.cycleSteps();
        std::vector<float> L, Rr;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok1 = r.renderPattern(L, Rr, st, 120.0);
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

        // 28 sixteenths at 120 bpm is 3.5 s, plus the ring-out
        const double seconds = (double)L.size() / 48000.0;
        const double wanted = (double)st * (60.0 / 120.0 * 0.25) + 1.5;
        snprintf(msg, sizeof msg, "%.2f s of audio for %lld sixteenths, rendered in %.0f ms",
                 seconds, (long long)st, ms);
        ok("the pattern renders to its own length", ok1 && std::fabs(seconds - wanted) < 0.1, msg);

        double peak = 0;
        for (float v : L) peak = std::max(peak, (double)std::fabs(v));
        snprintf(msg, sizeof msg, "peak %.3f", peak);
        ok("and it is not silence", peak > 0.01, msg);

        snprintf(msg, sizeof msg, "%.0f ms for %.1f s of audio", ms, seconds);
        ok("fast enough not to hang a window", ms < 1000.0, msg);

        // the engine is where it was: a render is not a performance
        ok("and the engine is left alone by it", !r.busy());
    }

    // ---- one stroke at a time, for a kit ----
    {
        mseq::MaghrebSeq r;
        r.setSampleRate(48000);
        int quiet = 0, sounded = 0;
        double longest = 0;
        for (int stroke = 0; stroke < mseq::kNumStrokes; stroke++) {
            std::vector<float> L, Rr;
            if (!r.renderStroke(L, Rr, 0, stroke, 2.0)) { quiet++; continue; }
            double peak = 0;
            size_t last = 0;
            for (size_t i = 0; i < L.size(); i++) {
                const double a = std::fabs(L[i]);
                peak = std::max(peak, a);
                if (a > 1e-4) last = i;
            }
            if (peak > 0.005) sounded++; else quiet++;
            longest = std::max(longest, (double)last / 48000.0);
        }
        snprintf(msg, sizeof msg, "%d of %d strokes sounded, longest %.2f s",
                 sounded, mseq::kNumStrokes, longest);
        ok("every stroke renders as its own one shot", sounded == mseq::kNumStrokes, msg);
        ok("and each one dies away inside two seconds", longest < 2.0);
    }

    // ---- the three exports, as the plug-in actually runs them ----
    {
        mseq::MaghrebSeq e;
        e.setSampleRate(48000);
        e.setTempo(100.0);
        const std::string folder = (dir / "exports").string();

        std::string err;
        const std::string mid = mseq::xport::writeMidi(e, folder, &err);
        snprintf(msg, sizeof msg, "%s", mid.empty() ? err.c_str()
                 : fs::path(mid).filename().string().c_str());
        ok("WRITE MIDI leaves a .mid named after the rhythm",
           !mid.empty() && fs::exists(mid) && fs::path(mid).extension() == ".mid", msg);

        const Parsed back = parseMidi(mid);
        snprintf(msg, sizeof msg, "%zu notes at %.0f bpm", back.notes.size(), back.bpm);
        ok("and it holds the pattern at the tempo it was made at",
           back.ok && !back.notes.empty() && std::fabs(back.bpm - 100.0) < 0.5, msg);

        const std::string wav = mseq::xport::writeLoop(e, folder, 48000, &err);
        uintmax_t size = wav.empty() ? 0 : fs::file_size(wav);
        snprintf(msg, sizeof msg, "%s, %.1f MB", wav.empty() ? err.c_str()
                 : fs::path(wav).filename().string().c_str(), size / 1e6);
        ok("RENDER LOOP leaves a wav of the pattern", !wav.empty() && size > 100000, msg);

        const int n = mseq::xport::writeKit(e, folder, 48000, &err);
        int found = 0;
        std::string first;
        for (const auto& d : fs::recursive_directory_iterator(folder))
            if (d.path().extension() == ".wav" && d.path() != fs::path(wav)) {
                found++;
                if (first.empty() || d.path().filename().string() < first)
                    first = d.path().filename().string();
            }
        snprintf(msg, sizeof msg, "%d written, %d on disk, first is \"%s\"", n, found, first.c_str());
        ok("RENDER KIT leaves one file per stroke, named by note",
           n == mseq::kNumStrokes && found == mseq::kNumStrokes &&
           first.rfind("36 ", 0) == 0, msg);
    }

    // ---- what goes out as MIDI is what sounded ----
    //
    // The wrapper sends whatever the engine reports having struck in the block
    // it just played. So the check is that playing the pattern through, block
    // by block, reports exactly the hits the pattern says are in it - in the
    // same order, on the same strokes.
    {
        mseq::MaghrebSeq e;
        e.setSampleRate(48000);
        e.setTempo(120.0);
        for (int lane = 1; lane < mseq::kLanes; lane++)
            e.setParam(mseq::laneParam(lane, mseq::kLaneMute), 1.f);
        e.setParam(mseq::laneParam(0, mseq::kLaneRhythm), 5.f);

        const int64_t steps = e.cycleSteps();
        const std::vector<mseq::MaghrebSeq::Hit> expect = e.pattern(steps);

        std::vector<int> heard;
        const int block = 256;
        const double secPerBeat = 60.0 / 120.0;
        const int64_t total = (int64_t)((double)steps * secPerBeat * 0.25 * 48000.0);
        std::vector<float> L((size_t)block), R((size_t)block);
        double beats = 0.0;
        // exactly the cycle, to the sample: a whole block over the end starts
        // the next turn, and step 0 comes round again
        for (int64_t at = 0; at < total; at += block) {
            const int n = (int)std::min<int64_t>(block, total - at);
            e.setTransport(beats, true);
            e.process(L.data(), R.data(), n);
            for (int i = 0; i < e.firedCount(); i++) heard.push_back(e.fired(i).stroke);
            beats += (double)n / (48000.0 * secPerBeat);
        }

        snprintf(msg, sizeof msg, "%zu struck, %zu in the pattern", heard.size(), expect.size());
        ok("playing the pattern strikes exactly what the pattern holds",
           heard.size() == expect.size(), msg);

        bool sameStrokes = heard.size() == expect.size();
        for (size_t i = 0; i < heard.size() && i < expect.size(); i++)
            if (heard[i] != expect[i].stroke) sameStrokes = false;
        ok("and on the same strokes, in the same order", sameStrokes);

        // a muted lane sends nothing, which is the whole point of reading the
        // hits rather than the rhythm string
        e.setParam(mseq::laneParam(0, mseq::kLaneMute), 1.f);
        int after = 0;
        beats = 0.0;
        for (int64_t at = 0; at < total; at += block) {
            const int n = (int)std::min<int64_t>(block, total - at);
            e.setTransport(beats, true);
            e.process(L.data(), R.data(), n);
            after += e.firedCount();
            beats += (double)n / (48000.0 * secPerBeat);
        }
        snprintf(msg, sizeof msg, "%d struck with the lane muted", after);
        ok("and a muted lane sends no notes either", after == 0, msg);
    }

    // ---- swing, in the audio rather than only in the description ----
    //
    // The offsets fireStep works out used to be handed to a trigger that threw
    // them away, so every drum started at the top of its block and the Swing
    // control did nothing you could hear. This measures where the hits land.
    {
        auto onsets = [](const std::vector<float>& x) {
            std::vector<int> out;
            int quiet = 1000;
            for (size_t i = 0; i < x.size(); i++) {
                if (std::fabs(x[i]) > 0.08f) {
                    if (quiet > 400) out.push_back((int)i);
                    quiet = 0;
                } else if (std::fabs(x[i]) < 0.01f) {
                    quiet++;
                }
            }
            return out;
        };

        auto play = [&](float swing) {
            mseq::MaghrebSeq e;
            e.setSampleRate(48000);
            e.setTempo(120.0);
            for (int lane = 1; lane < mseq::kLanes; lane++)
                e.setParam(mseq::laneParam(lane, mseq::kLaneMute), 1.f);
            e.setParam(mseq::laneParam(0, mseq::kLaneRhythm), 20.f);  // D.TkT.T.
            e.setParam(mseq::kSwing, swing);

            const int block = 256;
            const double secPerBeat = 0.5;
            std::vector<float> out;
            std::vector<float> L((size_t)block), R((size_t)block);
            double beats = 0.0;
            for (int b = 0; b < 400; b++) {                 // ~2.1 s
                e.setTransport(beats, true);
                e.process(L.data(), R.data(), block);
                out.insert(out.end(), L.begin(), L.end());
                beats += (double)block / (48000.0 * secPerBeat);
            }
            return out;
        };

        const std::vector<int> straight = onsets(play(0.f));
        const std::vector<int> swung = onsets(play(1.f));

        snprintf(msg, sizeof msg, "%zu straight, %zu swung", straight.size(), swung.size());
        ok("the same hits sound either way", straight.size() == swung.size() &&
                                             straight.size() >= 4, msg);

        // a sixteenth at 120 bpm is 6000 samples; two thirds of one is 3960
        int moved = 0, still = 0;
        for (size_t i = 0; i < straight.size() && i < swung.size(); i++) {
            const int d = swung[i] - straight[i];
            if (d > 3500 && d < 4400) moved++;
            else if (std::abs(d) < 400) still++;
        }
        snprintf(msg, sizeof msg, "%d hits held back, %d left where they were", moved, still);
        ok("swing moves the offbeats in the audio, and only them",
           moved > 0 && (moved + still) == (int)std::min(straight.size(), swung.size()), msg);

        // and the downbeats are still sample-accurate rather than quantised to
        // the top of a 256 sample block
        bool onGrid = true;
        for (size_t i = 0; i < straight.size(); i++) {
            const int fromStep = straight[i] % 6000;
            if (fromStep > 200 && fromStep < 5800) onGrid = false;
        }
        snprintf(msg, sizeof msg, "first onsets at %d, %d, %d",
                 straight.size() > 0 ? straight[0] : -1,
                 straight.size() > 1 ? straight[1] : -1,
                 straight.size() > 2 ? straight[2] : -1);
        ok("and hits land on their sixteenth, not on a block boundary", onGrid, msg);
    }

    fs::remove_all(dir, ec);
    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
