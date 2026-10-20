// Writing a WAV, in as many channels as it has.
//
// TapeGrain's own saveWav writes one channel, which is right for a grain
// engine that reads one. A repaired transfer is not that: azimuth error and
// channel imbalance are exactly the things worth keeping, so anything handed
// on from a repair stays as wide as it arrived.
//
// 24-bit, because that is what a capture chain is worth and what the rest of
// this family reads back without complaint.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace mwav {

// Reads a RIFF/WAVE file into planar float, one vector per channel, at
// whatever rate and depth it happens to be.
//
// Handles the two things real files do that a naive parser misses: chunks in
// any order - a JUNK chunk before fmt is common, and Ableton Move's own
// captures have one - and the four encodings that actually turn up, which are
// 16, 24 and 32-bit integer and 32-bit float. Anything else is refused rather
// than read as noise.
//
// Channels are kept as they are. A repair keeps both sides of a transfer;
// deciding what to do with more or fewer than two is the caller's business.
inline bool readWav(const std::string& path,
                    std::vector<std::vector<float>>& out,
                    int& sampleRate) {
    out.clear();
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    char riff[4], wave[4];
    uint32_t riffSize = 0;
    if (fread(riff, 1, 4, f) != 4 || fread(&riffSize, 4, 1, f) != 1 ||
        fread(wave, 1, 4, f) != 4 ||
        memcmp(riff, "RIFF", 4) || memcmp(wave, "WAVE", 4)) { fclose(f); return false; }

    uint16_t fmt = 0, nch = 0, bits = 0;
    uint32_t rate = 0;
    std::vector<uint8_t> data;
    bool haveFmt = false;

    for (;;) {
        char id[4];
        uint32_t sz = 0;
        if (fread(id, 1, 4, f) != 4 || fread(&sz, 4, 1, f) != 1) break;

        if (!memcmp(id, "fmt ", 4) && sz >= 16) {
            std::vector<uint8_t> b(sz);
            if (fread(b.data(), 1, sz, f) != sz) break;
            memcpy(&fmt,  &b[0],  2);
            memcpy(&nch,  &b[2],  2);
            memcpy(&rate, &b[4],  4);
            memcpy(&bits, &b[14], 2);
            // WAVE_FORMAT_EXTENSIBLE hides the real encoding in a subformat
            // GUID whose first two bytes are the plain tag.
            if (fmt == 0xFFFE && sz >= 26) memcpy(&fmt, &b[24], 2);
            haveFmt = true;
        } else if (!memcmp(id, "data", 4)) {
            data.resize(sz);
            if (fread(data.data(), 1, sz, f) != sz) data.clear();
            break;
        } else {
            if (fseek(f, (long)sz + (sz & 1), SEEK_CUR) != 0) break;   // chunks are word aligned
            continue;
        }
        if (sz & 1) fseek(f, 1, SEEK_CUR);
    }
    fclose(f);

    if (!haveFmt || data.empty() || nch == 0 || rate == 0) return false;

    const size_t bytes = bits / 8;
    if (bytes == 0) return false;
    const size_t frames = data.size() / (bytes * nch);
    if (frames == 0) return false;

    out.assign(nch, std::vector<float>(frames, 0.f));
    const uint8_t* p = data.data();

    if (fmt == 1 && bits == 16) {
        for (size_t i = 0; i < frames; i++)
            for (uint16_t c = 0; c < nch; c++) {
                int16_t v; memcpy(&v, p + (i * nch + c) * 2, 2);
                out[c][i] = (float)v / 32768.f;
            }
    } else if (fmt == 1 && bits == 24) {
        for (size_t i = 0; i < frames; i++)
            for (uint16_t c = 0; c < nch; c++) {
                const uint8_t* q = p + (i * nch + c) * 3;
                // built in the top three bytes so the sign comes along
                const int32_t v = (int32_t)((uint32_t)q[0] << 8 | (uint32_t)q[1] << 16 |
                                            (uint32_t)q[2] << 24);
                out[c][i] = (float)(v / 256) / 8388608.f;
            }
    } else if (fmt == 1 && bits == 32) {
        for (size_t i = 0; i < frames; i++)
            for (uint16_t c = 0; c < nch; c++) {
                int32_t v; memcpy(&v, p + (i * nch + c) * 4, 4);
                out[c][i] = (float)(v / 256) / 8388608.f;
            }
    } else if (fmt == 3 && bits == 32) {
        for (size_t i = 0; i < frames; i++)
            for (uint16_t c = 0; c < nch; c++) {
                float v; memcpy(&v, p + (i * nch + c) * 4, 4);
                out[c][i] = v;
            }
    } else {
        out.clear();
        return false;
    }

    sampleRate = (int)rate;
    return true;
}

// `channels` is planar: one vector per channel, all the same length.
inline bool writeWav24(const std::string& path,
                       const std::vector<std::vector<float>>& channels,
                       int sampleRate) {
    if (channels.empty() || channels[0].empty()) return false;
    const uint16_t nch = (uint16_t)channels.size();
    const size_t frames = channels[0].size();
    for (const auto& c : channels) if (c.size() != frames) return false;

    const uint16_t bits = 24;
    const uint16_t block = (uint16_t)(nch * bits / 8);
    const uint32_t byteRate = (uint32_t)sampleRate * block;
    const uint32_t dataBytes = (uint32_t)(frames * block);

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };

    fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    u32(16);
    u16(1);                       // PCM
    u16(nch);
    u32((uint32_t)sampleRate);
    u32(byteRate);
    u16(block);
    u16(bits);
    fwrite("data", 1, 4, f);
    u32(dataBytes);

    // Written a block at a time rather than a sample at a time: a side of tape
    // is millions of frames and three fwrite calls each is not free.
    std::vector<uint8_t> buf;
    buf.reserve(1 << 16);
    for (size_t i = 0; i < frames; i++) {
        for (uint16_t c = 0; c < nch; c++) {
            float v = channels[c][i];
            // Clipped rather than wrapped: an overshoot that wraps to the
            // opposite rail is a loud click where there was a quiet mistake.
            v = std::max(-1.f, std::min(1.f, v));
            const int32_t s = (int32_t)std::lround((double)v * 8388607.0);
            buf.push_back((uint8_t)(s & 0xFF));
            buf.push_back((uint8_t)((s >> 8) & 0xFF));
            buf.push_back((uint8_t)((s >> 16) & 0xFF));
        }
        if (buf.size() >= (1 << 16)) { fwrite(buf.data(), 1, buf.size(), f); buf.clear(); }
    }
    if (!buf.empty()) fwrite(buf.data(), 1, buf.size(), f);

    fclose(f);
    return true;
}

} // namespace mwav
