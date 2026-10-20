// Does the vendored msfa engine build, initialise and make a sound?
//
// Worth proving on its own before any patch design: if the integration is
// wrong, every later test would be measuring silence and I would be tuning
// patch data that never ran.

#include "msfa/synth.h"
#include "msfa/freqlut.h"
#include "msfa/sin.h"
#include "msfa/exp2.h"
#include "msfa/pitchenv.h"
#include "msfa/env.h"
#include "msfa/lfo.h"
#include "msfa/dx7note.h"
#include "msfa/controllers.h"
#include "msfa/fm_core.h"
#include "msfa/tuning.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static const double SR = 44100.0;

// A DX7 voice is 155 bytes of parameters plus a name; msfa wants the
// unpacked 156-byte form. Operators are stored in reverse: op6 first.
// Layout per operator (21 bytes): R1 R2 R3 R4 L1 L2 L3 L4 BP LD RD RC LC
// RS AMS KVS OL MODE FC FF DET
static void setOp(uint8_t* p, int op /*0=op1*/, int r1, int r2, int r3, int r4,
                  int l1, int l2, int l3, int l4, int outLevel,
                  int mode, int coarse, int fine, int detune) {
    uint8_t* o = p + (5 - op) * 21;      // op1 lives last in the block
    o[0] = r1; o[1] = r2; o[2] = r3; o[3] = r4;
    o[4] = l1; o[5] = l2; o[6] = l3; o[7] = l4;
    o[8] = 0;  o[9] = 0;  o[10] = 0; o[11] = 0; o[12] = 0;   // breakpoint etc
    o[13] = 0;                                                // rate scaling
    o[14] = 0;                                                // amp mod sens
    o[15] = 0;                                                // key vel sens
    o[16] = outLevel;
    o[17] = mode;                                             // 0 ratio, 1 fixed
    o[18] = coarse;
    o[19] = fine;
    o[20] = detune + 7;                                       // 0..14, 7 = none
}

// silence every operator, then switch on the ones a patch uses
static void blankPatch(uint8_t* p) {
    for (int i = 0; i < 156; i++) p[i] = 0;
    for (int op = 0; op < 6; op++)
        setOp(p, op, 99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 1, 0, 0);
    p[126] = 99; p[127] = 99; p[128] = 99; p[129] = 99;   // pitch eg rates
    p[130] = 50; p[131] = 50; p[132] = 50; p[133] = 50;   // pitch eg levels
    p[134] = 0;      // algorithm 1
    p[135] = 0;      // feedback
    p[136] = 1;      // osc key sync
    p[137] = 35;     // lfo speed
    p[138] = 0; p[139] = 0; p[140] = 0; p[141] = 0; p[142] = 0;
    p[143] = 3;      // transpose, 24 = none... 3 keeps it low for a drum
    for (int i = 145; i < 156; i++) p[i] = ' ';
    p[143] = 24;     // no transpose
}

int main() {
    char msg[300];

    // ---- the tables msfa needs before any note ----
    Freqlut::init(SR);
    Sin::init();
    Exp2::init();
    Tanh::init();          // the saturator table, not a separate Log2
    PitchEnv::init(SR);
    Env::init_sr(SR);
    Lfo::init(SR);
    ok("the engine tables initialise", true);

    auto tuning = createStandardTuning();
    ok("a standard tuning is available", tuning != nullptr);

    // Controllers carries a bare FmCore* that its constructor leaves
    // uninitialised, and Dx7Note::compute dereferences it on the first
    // sample. Nothing in the headers says so; it crashes immediately if you
    // miss it.
    FmCore fmcore;
    Controllers ctrls;
    ctrls.core = &fmcore;
    for (int i = 0; i < 132; i++) ctrls.values_[i] = 0;
    ctrls.values_[kControllerPitch] = 0x2000;
    ctrls.modwheel_cc = 0;
    ctrls.foot_cc = 0;
    ctrls.breath_cc = 0;
    ctrls.aftertouch_cc = 0;
    ctrls.opSwitch[0] = ctrls.opSwitch[1] = ctrls.opSwitch[2] = '1';
    ctrls.opSwitch[3] = ctrls.opSwitch[4] = ctrls.opSwitch[5] = '1';
    ctrls.opSwitch[6] = 0;
    // wheel, foot, breath and aftertouch default to a zero range already
    ctrls.refresh();

    // ---- a plain two-operator patch, carrier only ----
    uint8_t patch[156];
    blankPatch(patch);
    // op1 as a carrier that rings for a moment, op2 silent
    setOp(patch, 0, 99, 90, 60, 60, 99, 90, 0, 0, 99, 0, 1, 0, 0);
    setOp(patch, 1, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 1, 0, 0);

    Dx7Note note(tuning, nullptr);
    note.init(patch, 60, 100, 1, &ctrls);
    ok("a note initialises", true);

    // msfa renders exactly N samples at a time, in fixed point
    std::vector<float> out;
    double peak = 0;
    bool finite = true;
    for (int b = 0; b < 400; b++) {
        int32_t buf[N] = {0};
        note.compute(buf, 0, 0, &ctrls);
        for (int i = 0; i < N; i++) {
            const float s = (float)(buf[i] / (double)(1 << 24));
            if (!std::isfinite(s)) finite = false;
            peak = std::max(peak, (double)std::fabs(s));
            out.push_back(s);
        }
    }
    snprintf(msg, sizeof msg, "peak %.4f over %d samples", peak, (int)out.size());
    ok("it renders audio", peak > 1e-4, msg);
    ok("and the samples are finite", finite);

    // ---- the pitch is what was asked for ----
    {
        int cross = 0;
        // measure over the sounding part only
        size_t end = 0;
        for (size_t i = 0; i < out.size(); i++)
            if (std::fabs(out[i]) > peak * 0.05) end = i;
        for (size_t i = 1; i < end; i++)
            if ((out[i] < 0) != (out[i - 1] < 0)) cross++;
        const double hz = (double)cross / std::max<size_t>(1, end) * SR * 0.5;
        const double want = 440.0 * std::pow(2.0, (60 - 69) / 12.0);
        snprintf(msg, sizeof msg, "%.0f Hz measured, %.0f Hz expected", hz, want);
        ok("middle C comes out at middle C", std::fabs(hz - want) < want * 0.1, msg);
    }

    // ---- keyup releases it ----
    {
        Dx7Note n2(tuning, nullptr);
        n2.init(patch, 60, 100, 1, &ctrls);
        for (int b = 0; b < 20; b++) { int32_t buf[N] = {0}; n2.compute(buf, 0, 0, &ctrls); }
        ok("a fresh note is playing", n2.isPlaying());
        n2.keyup();
        double tail = 0;
        for (int b = 0; b < 2000; b++) {
            int32_t buf[N] = {0};
            n2.compute(buf, 0, 0, &ctrls);
            for (int i = 0; i < N; i++) tail = std::max(tail, std::fabs(buf[i] / (double)(1 << 24)));
            if (!n2.isPlaying()) break;
        }
        ok("and stops after keyup", !n2.isPlaying());
    }

    // ---- the modulator actually modulates ----
    {
        auto brightness = [&](const uint8_t* p) {
            Dx7Note n(tuning, nullptr);
            n.init(p, 60, 100, 1, &ctrls);
            std::vector<float> x;
            double pk = 0;
            for (int b = 0; b < 200; b++) {
                int32_t buf[N] = {0};
                n.compute(buf, 0, 0, &ctrls);
                for (int i = 0; i < N; i++) {
                    const float s = (float)(buf[i] / (double)(1 << 24));
                    x.push_back(s);
                    pk = std::max(pk, (double)std::fabs(s));
                }
            }
            size_t end = 0;
            for (size_t i = 0; i < x.size(); i++) if (std::fabs(x[i]) > pk * 0.05) end = i;
            int c = 0;
            for (size_t i = 1; i < end; i++) if ((x[i] < 0) != (x[i - 1] < 0)) c++;
            return (double)c / std::max<size_t>(1, end) * SR * 0.5;
        };
        uint8_t quiet[156], loud[156];
        blankPatch(quiet); blankPatch(loud);
        setOp(quiet, 0, 99, 90, 60, 60, 99, 90, 0, 0, 99, 0, 1, 0, 0);
        setOp(quiet, 1, 99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 3, 0, 0);
        setOp(loud, 0, 99, 90, 60, 60, 99, 90, 0, 0, 99, 0, 1, 0, 0);
        setOp(loud, 1, 99, 99, 99, 99, 99, 99, 99, 0, 95, 0, 3, 0, 0);

        const double a = brightness(quiet), b = brightness(loud);
        snprintf(msg, sizeof msg, "%.0f Hz with the modulator off, %.0f Hz with it up", a, b);
        ok("opening the modulator adds harmonics", b > a * 1.3, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
