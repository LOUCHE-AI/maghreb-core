// Cartridge reading, checked three ways.
//
// A round trip against a packer alone would only prove the two halves agree
// with each other, so it is not the whole test: the unpacked voices are also
// rendered through msfa, which is the thing that will actually be handed
// them, and a real sysex file that is not a DX7 cartridge is fed in to make
// sure it is refused rather than decoded into rubbish.

#include "dx_sysex.h"
#include "dx_engine.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

// The inverse of Dexed's unpackProgram, written from it field by field, so a
// round trip exercises every bit field and every mask.
static void packVoice(const uint8_t* in /*156*/, uint8_t* bulk /*128*/) {
    for (int op = 0; op < 6; op++) {
        const uint8_t* o = in + op * 21;
        uint8_t* b = bulk + op * 17;
        for (int i = 0; i < 11; i++) b[i] = o[i] & 0x7F;
        b[11] = (uint8_t)((o[11] & 3) | ((o[12] & 3) << 2));
        b[12] = (uint8_t)((o[13] & 7) | ((o[20] & 0x0F) << 3));
        b[13] = (uint8_t)((o[14] & 3) | ((o[15] & 7) << 2));
        b[14] = o[16] & 0x7F;
        b[15] = (uint8_t)((o[17] & 1) | ((o[18] & 0x1F) << 1));
        b[16] = o[19] & 0x7F;
    }
    for (int i = 0; i < 8; i++) bulk[102 + i] = in[126 + i] & 0x7F;
    bulk[110] = in[134] & 0x1F;
    bulk[111] = (uint8_t)((in[135] & 7) | ((in[136] & 1) << 3));
    bulk[112] = in[137]; bulk[113] = in[138];
    bulk[114] = in[139]; bulk[115] = in[140];
    bulk[116] = (uint8_t)((in[141] & 1) | ((in[142] & 7) << 1) | ((in[143] & 7) << 4));
    bulk[117] = in[144];
    for (int i = 0; i < 10; i++) bulk[118 + i] = in[145 + i];
}

// a voice with every field set to something distinctive
static void makeVoice(uint8_t* v, int seed, const char* name) {
    for (int i = 0; i < 156; i++) v[i] = 0;
    for (int op = 0; op < 6; op++) {
        uint8_t* o = v + op * 21;
        for (int i = 0; i < 8; i++) o[i] = (uint8_t)((seed * 7 + op * 11 + i * 3) % 100);
        o[8] = (uint8_t)((seed + op) % 100);      // break point
        o[9] = (uint8_t)((seed * 3 + op) % 100);  // left depth
        o[10] = (uint8_t)((seed * 5 + op) % 100); // right depth
        o[11] = (uint8_t)((seed + op) % 4);       // left curve
        o[12] = (uint8_t)((seed + op + 1) % 4);   // right curve
        o[13] = (uint8_t)((seed + op) % 8);       // rate scaling
        o[14] = (uint8_t)((seed + op) % 4);       // amp mod sens
        o[15] = (uint8_t)((seed + op) % 8);       // key velocity sens
        o[16] = (uint8_t)((seed * 9 + op) % 100); // output level
        o[17] = (uint8_t)((seed + op) % 2);       // mode
        o[18] = (uint8_t)((seed + op) % 32);      // coarse
        o[19] = (uint8_t)((seed * 2 + op) % 100); // fine
        o[20] = (uint8_t)((seed + op) % 15);      // detune
    }
    for (int i = 0; i < 8; i++) v[126 + i] = (uint8_t)((seed + i * 5) % 100);
    v[134] = (uint8_t)(seed % 32);   // algorithm
    v[135] = (uint8_t)(seed % 8);    // feedback
    v[136] = (uint8_t)(seed % 2);    // key sync
    v[137] = (uint8_t)(seed % 100);
    v[138] = (uint8_t)((seed * 2) % 100);
    v[139] = (uint8_t)((seed * 3) % 100);
    v[140] = (uint8_t)((seed * 4) % 100);
    v[141] = (uint8_t)(seed % 2);    // lfo sync
    v[142] = (uint8_t)(seed % 6);    // lfo wave
    v[143] = (uint8_t)(seed % 8);    // pitch mod sens
    v[144] = (uint8_t)(seed % 49);   // transpose
    for (int i = 0; i < 10; i++) v[145 + i] = name[i] ? name[i] : ' ';
}

static void writeCart(const char* path, const uint8_t voices[][156], int n) {
    std::vector<uint8_t> f;
    f.push_back(0xF0); f.push_back(0x43); f.push_back(0x00);
    f.push_back(0x09); f.push_back(0x20); f.push_back(0x00);
    for (int v = 0; v < 32; v++) {
        uint8_t bulk[128] = {0};
        packVoice(voices[v < n ? v : 0], bulk);
        f.insert(f.end(), bulk, bulk + 128);
    }
    uint8_t sum = 0;
    for (size_t i = 6; i < f.size(); i++) sum = (uint8_t)(sum + f[i]);
    f.push_back((uint8_t)((~sum + 1) & 0x7F));
    f.push_back(0xF7);
    FILE* h = fopen(path, "wb");
    fwrite(f.data(), 1, f.size(), h);
    fclose(h);
}

int main() {
    char msg[400];

    // ---- round trip through a real cartridge layout ----
    static uint8_t voices[32][156];
    const char* names[4] = {"BRASS   1 ", "E.PIANO 1 ", "STRINGS 3 ", "MARIMBA   "};
    for (int v = 0; v < 32; v++) makeVoice(voices[v], v + 1, names[v % 4]);
    writeCart("test_cart.syx", voices, 32);

    std::vector<mdx::SysexVoice> got;
    const int n = mdx::loadSysex("test_cart.syx", got);
    snprintf(msg, sizeof msg, "%d voices", n);
    ok("a 32-voice cartridge reads back", n == 32 && (int)got.size() == 32, msg);

    if (n == 32) {
        int mismatches = 0;
        std::string first;
        for (int v = 0; v < 32; v++)
            for (int i = 0; i < 155; i++)
                if (got[v].data[i] != voices[v][i]) {
                    mismatches++;
                    if (first.empty()) {
                        char b[100];
                        snprintf(b, sizeof b, "voice %d byte %d: %d vs %d",
                                 v, i, got[v].data[i], voices[v][i]);
                        first = b;
                    }
                }
        snprintf(msg, sizeof msg, "%d mismatched bytes of %d", mismatches, 32 * 155);
        ok("every byte of every voice survives packing and unpacking",
           mismatches == 0, first.empty() ? msg : first);

        ok("voice names come back, trimmed",
           std::string(got[0].name) == "BRASS   1" &&
           std::string(got[1].name) == "E.PIANO 1",
           std::string("\"") + got[0].name + "\", \"" + got[1].name + "\"");
    }

    // ---- the decoded voices actually play ----
    {
        mdx::DxEngine eng;
        eng.setSampleRate(44100.0);
        int sounded = 0;
        bool finite = true;
        for (int v = 0; v < 8 && v < (int)got.size(); v++) {
            eng.allOff();
            eng.noteOn(got[v].data, 60, 99, 0.5f, 1.f, 0);
            std::vector<float> l(8192, 0.f), r(8192, 0.f);
            eng.render(l.data(), r.data(), 8192);
            double pk = 0;
            for (size_t i = 0; i < l.size(); i++) {
                if (!std::isfinite(l[i])) finite = false;
                pk = std::max(pk, (double)std::fabs(l[i]));
            }
            if (pk > 1e-5) sounded++;
        }
        snprintf(msg, sizeof msg, "%d of 8 made a sound", sounded);
        ok("the decoded voices play through the engine", sounded >= 6, msg);
        ok("and none of them produces NaN or inf", finite);
    }

    // ---- a real sysex file that is not a DX7 cartridge ----
    {
        const char* fw = "C:\\Users\\welal\\AppData\\Roaming\\Components\\products"
                         "\\bsii\\bsii-firmware-696.syx";
        std::vector<mdx::SysexVoice> v;
        const int got2 = mdx::loadSysex(fw, v);
        snprintf(msg, sizeof msg, "%d voices from a Novation firmware dump", got2);
        ok("a sysex file that is not a DX7 cartridge is refused", got2 == 0, msg);
    }

    // ---- the awkward cases ----
    {
        std::vector<mdx::SysexVoice> v;
        ok("a missing file is refused", mdx::loadSysex("no_such.syx", v) == 0);
        ok("an empty path is refused", mdx::loadSysex("", v) == 0);

        FILE* h = fopen("test_short.syx", "wb");
        const uint8_t hdr[6] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
        fwrite(hdr, 1, 6, h);
        std::vector<uint8_t> half(2000, 0x40);
        fwrite(half.data(), 1, half.size(), h);
        fclose(h);
        snprintf(msg, sizeof msg, "%d voices", mdx::loadSysex("test_short.syx", v));
        ok("a cartridge cut in half is refused rather than half-read",
           mdx::loadSysex("test_short.syx", v) == 0, msg);

        // a header that does not start at byte zero must still be found
        h = fopen("test_offset.syx", "wb");
        const uint8_t junk[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        fwrite(junk, 1, 9, h);
        FILE* src = fopen("test_cart.syx", "rb");
        std::vector<uint8_t> all;
        uint8_t buf[512];
        size_t rd;
        while ((rd = fread(buf, 1, sizeof buf, src)) > 0) all.insert(all.end(), buf, buf + rd);
        fclose(src);
        fwrite(all.data(), 1, all.size(), h);
        fclose(h);
        const int off = mdx::loadSysex("test_offset.syx", v);
        snprintf(msg, sizeof msg, "%d voices past nine bytes of junk", off);
        ok("a header that is not at the start of the file is still found",
           off == 32, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
