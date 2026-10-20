// Check the two ways a sample gets into TapeGrain: loading a WAV from disk,
// and recording the track input.
//
// The WAV reader is tested against the awkward cases that actually occur -
// a JUNK chunk before fmt, stereo, and a non-44.1k rate - because Ableton
// Move's own captures are stereo 48k with a JUNK chunk, and a naive reader
// silently mangles them.

#include "tapegrain_dsp.h"
#include "tapegrain_io.h"
#include <cstdio>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* label, bool cond, const std::string& extra = "") {
    printf("%s  %s%s%s\n", cond ? "PASS " : "FAIL ", label,
           extra.empty() ? "" : "  ", extra.c_str());
    if (!cond) fails++;
}

// writes a wav with a JUNK chunk before fmt, N channels, given rate
static void writeAwkwardWav(const std::string& path, int ch, int sr, int frames) {
    FILE* f = fopen(path.c_str(), "wb");
    uint32_t u; uint16_t s;
    std::vector<int16_t> pcm((size_t)frames * ch);
    for (int i = 0; i < frames; i++)
        for (int c = 0; c < ch; c++)
            pcm[(size_t)i * ch + c] = (int16_t)(20000 * sin(i * 0.05 + c));

    uint32_t dataBytes = (uint32_t)(pcm.size() * 2);
    uint32_t junkBytes = 52;
    fwrite("RIFF", 1, 4, f); u = 4 + (8 + junkBytes) + 24 + (8 + dataBytes);
    fwrite(&u, 4, 1, f); fwrite("WAVE", 1, 4, f);

    fwrite("JUNK", 1, 4, f); fwrite(&junkBytes, 4, 1, f);
    std::vector<char> junk(junkBytes, 0); fwrite(junk.data(), 1, junkBytes, f);

    fwrite("fmt ", 1, 4, f); u = 16; fwrite(&u, 4, 1, f);
    s = 1; fwrite(&s, 2, 1, f); s = (uint16_t)ch; fwrite(&s, 2, 1, f);
    u = sr; fwrite(&u, 4, 1, f); u = sr * 2 * ch; fwrite(&u, 4, 1, f);
    s = (uint16_t)(2 * ch); fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);

    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(pcm.data(), 1, dataBytes, f);
    fclose(f);
}

int main() {
    const int SR = 44100;
    char msg[160];

    // ---------- loading ----------
    writeAwkwardWav("io_stereo48k.wav", 2, 48000, 24000);
    tg::WavData w = tg::loadWav("io_stereo48k.wav");
    ok("reads a wav with JUNK before fmt", w.ok);
    snprintf(msg, sizeof msg, "%d ch, %d Hz, %zu frames", w.channels, w.sampleRate, w.mono.size());
    ok("stereo summed to mono", w.channels == 2 && w.mono.size() == 24000, msg);
    ok("sample rate read correctly", w.sampleRate == 48000);

    std::vector<float> rs = tg::resample(w.mono, w.sampleRate, SR);
    snprintf(msg, sizeof msg, "%zu -> %zu frames", w.mono.size(), rs.size());
    ok("resampled to the host rate", rs.size() == (size_t)(24000.0 * SR / 48000), msg);

    ok("a missing file fails cleanly", !tg::loadWav("does_not_exist.wav").ok);
    ok("a non-wav fails cleanly", !tg::loadWav("test_io.cpp").ok);

    // round trip
    ok("saves a wav", tg::saveWav("io_roundtrip.wav", rs, SR));
    tg::WavData back = tg::loadWav("io_roundtrip.wav");
    double err = 0;
    if (back.ok && back.mono.size() == rs.size())
        for (size_t i = 0; i < rs.size(); i++)
            err = std::max(err, (double)std::fabs(back.mono[i] - rs[i]));
    snprintf(msg, sizeof msg, "max error %.5f (16-bit quantisation)", err);
    ok("round trips within 16-bit precision", back.ok && err < 0.001, msg);

    // the loaded sample must actually granulate
    tg::TapeGrain g;
    g.setSampleRate(SR);
    g.setSample(rs.data(), (int)rs.size());
    ok("loaded sample accepted", g.hasSample());
    {
        float bl[512], br[512];
        double peak = 0;
        g.setParam(tg::kMotion, (float)tg::mScan);
        g.noteOn(60, 1.f);              // it is an instrument: hold a note
        for (int b = 0; b < 40; b++) {
            g.process(bl, br, 512);
            for (int i = 0; i < 512; i++) peak = std::max(peak, (double)std::fabs(bl[i]));
        }
        snprintf(msg, sizeof msg, "peak %.3f", peak);
        ok("granulates the loaded file", peak > 1e-4, msg);
    }

    // ---------- recording ----------
    tg::TapeGrain r;
    r.setSampleRate(SR);
    ok("starts not recording", !r.isRecording());

    r.setRecording(true, 2.0);
    ok("arms", r.isRecording());

    // feed two seconds of a tone in blocks, as a host would
    float inL[512], inR[512], oL[512], oR[512];
    int fed = 0;
    for (int b = 0; b < 200; b++) {
        for (int i = 0; i < 512; i++) {
            inL[i] = 0.6f * sinf((fed + i) * 0.01f);
            inR[i] = inL[i];
        }
        r.captureInput(inL, inR, 512);
        r.process(oL, oR, 512);          // must stay silent: nothing loaded yet
        fed += 512;
    }
    snprintf(msg, sizeof msg, "%.2f s captured", r.recordedSeconds());
    ok("captures input while armed", r.recordedSeconds() > 1.0, msg);
    ok("stops itself at the limit", !r.isRecording(), msg);

    ok("the take became the sample", r.hasSample());
    {
        float bl[512], br[512];
        double peak = 0;
        r.setParam(tg::kMotion, (float)tg::mScan);
        r.noteOn(60, 1.f);              // it is an instrument: hold a note
        for (int b = 0; b < 40; b++) {
            r.process(bl, br, 512);
            for (int i = 0; i < 512; i++) peak = std::max(peak, (double)std::fabs(bl[i]));
        }
        snprintf(msg, sizeof msg, "peak %.3f", peak);
        ok("granulates the recording", peak > 1e-4, msg);
    }

    // saving a take back out, so it can be reloaded later
    ok("saves the take", tg::saveWav("io_take.wav", r.take(), SR));

    // arming again must discard the old take rather than append
    r.setRecording(true, 1.0);
    ok("re-arming clears the previous take", r.recordedSeconds() < 0.01);
    r.setRecording(false);

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
