// Reading DX7 cartridges.
//
// A bulk dump is 4104 bytes: a six-byte header, thirty-two voices packed into
// 128 bytes each, a checksum and F7. msfa wants the 156-byte unpacked form,
// so the bit fields have to be pulled apart.
//
// The unpacking below follows Dexed's own Cartridge::unpackProgram field for
// field, including which bits the sysex spec says are "don't care" and must
// be masked off - a real cartridge from the wild often has rubbish in them,
// and a patch that ignores the masks decodes into nonsense rather than
// failing outright, which is the worst way for this to go wrong.
//
// Framework-free, so a cartridge can be decoded and checked offline.

#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mdx {

struct SysexVoice {
    uint8_t data[156] = {0};      // unpacked, as Dx7Note::init wants it
    char name[11] = {0};
};

static const int kVoicesPerCart = 32;

// Pulls one packed 128-byte voice out of `bulk` into the 156-byte form.
inline void unpackVoice(const uint8_t* bulk, uint8_t* out) {
    for (int op = 0; op < 6; op++) {
        const uint8_t* b = bulk + op * 17;
        uint8_t* o = out + op * 21;

        for (int i = 0; i < 11; i++) o[i] = b[i] & 0x7F;   // rates, levels, scaling

        const uint8_t curves = b[11] & 0x0F;
        o[11] = curves & 3;                                // left curve
        o[12] = (curves >> 2) & 3;                         // right curve

        const uint8_t detuneRs = b[12] & 0x7F;
        o[13] = detuneRs & 7;                              // rate scaling
        o[20] = (detuneRs >> 3) & 0x7F;                    // detune

        const uint8_t kvsAms = b[13] & 0x1F;
        o[14] = kvsAms & 3;                                // amp mod sensitivity
        o[15] = (kvsAms >> 2) & 7;                         // key velocity sens

        o[16] = b[14] & 0x7F;                              // output level

        const uint8_t coarseMode = b[15] & 0x3F;
        o[17] = coarseMode & 1;                            // 0 ratio, 1 fixed
        o[18] = (coarseMode >> 1) & 0x1F;                  // coarse
        o[19] = b[16] & 0x7F;                              // fine
    }

    for (int i = 0; i < 8; i++) out[126 + i] = bulk[102 + i] & 0x7F;   // pitch EG
    out[134] = bulk[110] & 0x1F;                                       // algorithm

    const uint8_t oksFb = bulk[111] & 0x0F;
    out[135] = oksFb & 7;                                              // feedback
    out[136] = oksFb >> 3;                                             // key sync

    out[137] = bulk[112] & 0x7F;   // lfo speed
    out[138] = bulk[113] & 0x7F;   // lfo delay
    out[139] = bulk[114] & 0x7F;   // pitch mod depth
    out[140] = bulk[115] & 0x7F;   // amp mod depth

    const uint8_t lfo = bulk[116] & 0x7F;
    out[141] = lfo & 1;            // lfo sync
    out[142] = (lfo >> 1) & 7;     // lfo waveform
    out[143] = lfo >> 4;           // pitch mod sensitivity

    out[144] = bulk[117] & 0x7F;   // transpose
    for (int i = 0; i < 10; i++) out[145 + i] = bulk[118 + i] & 0x7F;
    out[155] = 0;
}

// Reads a .syx. Accepts a 32-voice bulk dump, and tolerates the file being
// wrapped in more than one message or carrying trailing bytes, because plenty
// of cartridges in circulation are. Returns how many voices were read.
inline int loadSysex(const char* path, std::vector<SysexVoice>& out) {
    out.clear();
    if (!path || !*path) return 0;

    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    std::vector<uint8_t> buf;
    uint8_t chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0)
        buf.insert(buf.end(), chunk, chunk + got);
    fclose(f);
    if (buf.size() < 6 + 128) return 0;

    // Find a bulk-dump header rather than assuming the file starts with one:
    // F0 43 0n 09, where the 09 is the 32-voice format.
    size_t at = std::string::npos;
    for (size_t i = 0; i + 4 < buf.size(); i++) {
        if (buf[i] == 0xF0 && buf[i + 1] == 0x43 &&
            (buf[i + 2] & 0xF0) == 0x00 && buf[i + 3] == 0x09) { at = i; break; }
    }

    if (at == std::string::npos) {
        // Some banks are the raw 4096 bytes with no wrapper at all. The size
        // has to match exactly: accepting anything merely large enough means
        // a firmware dump, or any other sysex, decodes into thirty-two voices
        // of rubbish instead of being refused.
        if (buf.size() != 4096) return 0;
        for (int v = 0; v < kVoicesPerCart; v++) {
            SysexVoice sv;
            unpackVoice(buf.data() + v * 128, sv.data);
            std::memcpy(sv.name, sv.data + 145, 10);
            out.push_back(sv);
        }
        return (int)out.size();
    }

    const size_t body = at + 6;
    if (body + 4096 > buf.size()) return 0;
    for (int v = 0; v < kVoicesPerCart; v++) {
        SysexVoice sv;
        unpackVoice(buf.data() + body + v * 128, sv.data);
        std::memcpy(sv.name, sv.data + 145, 10);
        // Names are padded with spaces; trim so a menu reads cleanly. A few
        // voices in the wild carry bytes outside printable ASCII - the DX7's
        // own character set has glyphs that do not map - so those become
        // spaces rather than control codes in the display.
        for (int i = 0; i < 10; i++)
            if (sv.name[i] && (sv.name[i] < 32 || sv.name[i] > 126)) sv.name[i] = ' ';
        for (int i = 9; i >= 0 && (sv.name[i] == ' ' || sv.name[i] == 0); i--)
            sv.name[i] = 0;
        out.push_back(sv);
    }
    return (int)out.size();
}

} // namespace mdx
