// WAV load/save and folder scanning for TapeGrain.
//
// Kept separate from the DSP so the engine stays free of file I/O and can be
// rendered in a test without touching the disk.
//
// The reader handles the two things real files do that naive parsers miss:
// chunks in any order (a JUNK chunk before fmt is common - Ableton Move's own
// captures have one), and stereo sources, which are summed to mono because the
// grain engine reads a single channel.

#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace tg {

struct WavData {
    std::vector<float> mono;
    int sampleRate = 44100;
    int channels = 0;
    bool ok = false;
};

inline WavData loadWav(const std::string& path) {
    WavData out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;

    char riff[4], wave[4];
    uint32_t riffSize = 0;
    if (fread(riff, 1, 4, f) != 4 || fread(&riffSize, 4, 1, f) != 1 ||
        fread(wave, 1, 4, f) != 4 ||
        memcmp(riff, "RIFF", 4) || memcmp(wave, "WAVE", 4)) {
        fclose(f); return out;
    }

    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    std::vector<uint8_t> data;

    // walk the chunks; do not assume fmt comes first
    for (;;) {
        char id[4];
        uint32_t sz = 0;
        if (fread(id, 1, 4, f) != 4) break;
        if (fread(&sz, 4, 1, f) != 1) break;

        if (!memcmp(id, "fmt ", 4)) {
            std::vector<uint8_t> b(sz);
            if (fread(b.data(), 1, sz, f) != sz) break;
            if (sz >= 16) {
                memcpy(&fmt, &b[0], 2);
                memcpy(&ch, &b[2], 2);
                memcpy(&rate, &b[4], 4);
                memcpy(&bits, &b[14], 2);
            }
        } else if (!memcmp(id, "data", 4)) {
            data.resize(sz);
            if (fread(data.data(), 1, sz, f) != sz) data.clear();
        } else {
            if (fseek(f, sz + (sz & 1), SEEK_CUR) != 0) break;
            continue;
        }
        if (sz & 1) fseek(f, 1, SEEK_CUR);          // chunks are word aligned
        if (!data.empty() && ch) break;
    }
    fclose(f);

    if (data.empty() || !ch || (fmt != 1 && fmt != 3)) return out;

    size_t frames = 0;
    if (fmt == 1 && bits == 16) {
        frames = data.size() / (2 * ch);
        out.mono.resize(frames);
        const int16_t* s = (const int16_t*)data.data();
        for (size_t i = 0; i < frames; i++) {
            float acc = 0;
            for (int c = 0; c < ch; c++) acc += s[i * ch + c] / 32768.0f;
            out.mono[i] = acc / ch;                  // stereo summed to mono
        }
    } else if (fmt == 1 && bits == 24) {
        frames = data.size() / (3 * ch);
        out.mono.resize(frames);
        for (size_t i = 0; i < frames; i++) {
            float acc = 0;
            for (int c = 0; c < ch; c++) {
                const uint8_t* p = &data[(i * ch + c) * 3];
                int32_t v = (p[0] << 8) | (p[1] << 16) | (p[2] << 24);
                acc += (v >> 8) / 8388608.0f;
            }
            out.mono[i] = acc / ch;
        }
    } else if (fmt == 3 && bits == 32) {
        frames = data.size() / (4 * ch);
        out.mono.resize(frames);
        const float* s = (const float*)data.data();
        for (size_t i = 0; i < frames; i++) {
            float acc = 0;
            for (int c = 0; c < ch; c++) acc += s[i * ch + c];
            out.mono[i] = acc / ch;
        }
    } else {
        return out;                                  // unsupported depth
    }

    out.sampleRate = (int)rate;
    out.channels = ch;
    out.ok = !out.mono.empty();
    return out;
}

inline bool saveWav(const std::string& path, const std::vector<float>& mono, int sr) {
    if (mono.empty()) return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    std::vector<int16_t> pcm(mono.size());
    for (size_t i = 0; i < mono.size(); i++) {
        float v = mono[i] < -1.f ? -1.f : (mono[i] > 1.f ? 1.f : mono[i]);
        pcm[i] = (int16_t)(v * 32767.f);
    }
    uint32_t bytes = (uint32_t)(pcm.size() * 2), u; uint16_t s;
    fwrite("RIFF", 1, 4, f); u = 36 + bytes; fwrite(&u, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); u = 16; fwrite(&u, 4, 1, f);
    s = 1; fwrite(&s, 2, 1, f); s = 1; fwrite(&s, 2, 1, f);
    u = sr; fwrite(&u, 4, 1, f); u = sr * 2; fwrite(&u, 4, 1, f);
    s = 2; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(pcm.data(), 1, bytes, f);
    fclose(f);
    return true;
}

// Resample by linear interpolation. A sample recorded at another rate would
// otherwise play back at the wrong pitch.
inline std::vector<float> resample(const std::vector<float>& in, int from, int to) {
    if (in.empty() || from <= 0 || to <= 0 || from == to) return in;
    size_t n = (size_t)((double)in.size() * to / from);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; i++) {
        double pos = (double)i * from / to;
        size_t i0 = (size_t)pos;
        size_t i1 = std::min(i0 + 1, in.size() - 1);
        double f = pos - i0;
        out[i] = (float)(in[i0] * (1.0 - f) + in[i1] * f);
    }
    return out;
}

} // namespace tg
