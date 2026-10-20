// Measure the engine rather than assume it.
//
// Two things have to be known before a stroke can be turned into a DX7 patch:
// what frequency a fixed-mode operator actually produces for a given coarse
// and fine, and how long a DX7 envelope rate actually takes to decay. Both
// are buried in fixed-point tables inside msfa. Rather than re-derive them
// and hope, this renders the engine and reads the answers off the audio, then
// prints a table to paste into dx_patch.h.

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
#include <vector>

static const double SR = 44100.0;

static void setOp(uint8_t* p, int op, int r1, int r2, int r3, int r4,
                  int l1, int l2, int l3, int l4, int outLevel,
                  int mode, int coarse, int fine, int detune) {
    uint8_t* o = p + (5 - op) * 21;
    o[0] = r1; o[1] = r2; o[2] = r3; o[3] = r4;
    o[4] = l1; o[5] = l2; o[6] = l3; o[7] = l4;
    o[8] = 0; o[9] = 0; o[10] = 0; o[11] = 0; o[12] = 0;
    o[13] = 0; o[14] = 0; o[15] = 0;
    o[16] = outLevel; o[17] = mode; o[18] = coarse; o[19] = fine;
    o[20] = detune + 7;
}

static void blankPatch(uint8_t* p) {
    for (int i = 0; i < 156; i++) p[i] = 0;
    for (int op = 0; op < 6; op++)
        setOp(p, op, 99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 1, 0, 0);
    p[126] = 99; p[127] = 99; p[128] = 99; p[129] = 99;
    p[130] = 50; p[131] = 50; p[132] = 50; p[133] = 50;
    p[134] = 4;    // algorithm 5: three two-operator stacks
    p[135] = 0;
    p[136] = 1;
    p[137] = 35;
    p[144] = 24;   // no transpose
    for (int i = 145; i < 156; i++) p[i] = ' ';
}

static FmCore g_core;
static Controllers g_ctrls;
static std::shared_ptr<TuningState> g_tuning;

static std::vector<float> render(const uint8_t* patch, int blocks) {
    Dx7Note n(g_tuning, nullptr);
    n.init(patch, 60, 99, 1, &g_ctrls);
    std::vector<float> out;
    out.reserve(blocks * N);
    for (int b = 0; b < blocks; b++) {
        int32_t buf[N] = {0};
        n.compute(buf, 0, 0, &g_ctrls);
        for (int i = 0; i < N; i++) out.push_back((float)(buf[i] / (double)(1 << 24)));
    }
    return out;
}

static double peakOf(const std::vector<float>& x) {
    double p = 0;
    for (size_t i = 0; i < x.size(); i++) p = std::max(p, (double)std::fabs(x[i]));
    return p;
}

// crossing rate over the sounding part
static double pitchOf(const std::vector<float>& x) {
    const double pk = peakOf(x);
    if (pk <= 0) return 0;
    size_t end = 0;
    for (size_t i = 0; i < x.size(); i++) if (std::fabs(x[i]) > pk * 0.05) end = i;
    if (end < 64) return 0;
    int c = 0;
    for (size_t i = 1; i < end; i++) if ((x[i] < 0) != (x[i - 1] < 0)) c++;
    return (double)c / end * SR * 0.5;
}

// time for the envelope to fall 60 dB from its peak
static double decayOf(const std::vector<float>& x) {
    const double pk = peakOf(x);
    if (pk <= 0) return 0;
    const double floorAmp = pk * 0.001;
    // Peak-hold over short chunks rather than an exponential follower: a
    // follower's own release time becomes the answer once the envelope is
    // faster than it, and the fast rates are exactly where a drum lives. A
    // chunk of 32 samples resolves to under a millisecond and cannot smear.
    const int chunk = 32;
    for (size_t i = 0; i + chunk <= x.size(); i += chunk) {
        double m = 0;
        for (int k = 0; k < chunk; k++) m = std::max(m, (double)std::fabs(x[i + k]));
        if (i > (size_t)chunk && m < floorAmp) return (double)i / SR;
    }
    return (double)x.size() / SR;
}

int main() {
    Freqlut::init(SR);
    Sin::init();
    Exp2::init();
    Tanh::init();
    PitchEnv::init(SR);
    Env::init_sr(SR);
    Lfo::init(SR);
    g_tuning = createStandardTuning();

    g_ctrls.core = &g_core;
    for (int i = 0; i < 132; i++) g_ctrls.values_[i] = 0;
    g_ctrls.values_[kControllerPitch] = 0x2000;
    g_ctrls.modwheel_cc = g_ctrls.foot_cc = 0;
    g_ctrls.breath_cc = g_ctrls.aftertouch_cc = 0;
    strcpy(g_ctrls.opSwitch, "111111");
    g_ctrls.refresh();

    // ---- fixed frequency: does 10^(V/100) hold? ----
    printf("// fixed-mode frequency check: coarse/fine -> measured Hz\n");
    const int probe[] = {150, 200, 250, 262, 300, 350};   // V values
    for (int k = 0; k < 6; k++) {
        const int V = probe[k];
        uint8_t p[156];
        blankPatch(p);
        setOp(p, 0, 99, 0, 0, 0, 99, 99, 99, 99, 99, 1, V / 100, V % 100, 0);
        const double want = std::pow(10.0, V / 100.0);
        const double got = pitchOf(render(p, 300));
        printf("//   V=%3d  want %8.1f Hz   measured %8.1f Hz   (%+.1f%%)\n",
               V, want, got, 100.0 * (got - want) / want);
    }

    // ---- envelope rate -> seconds ----
    // R1 instant to L1=99, then R2 decaying to L2=0. The rate under test is
    // R2, so what is measured is that rate alone.
    printf("\n// seconds for a 60 dB decay at each DX7 envelope rate\n");
    printf("static const float kRateSeconds[100] = {\n");
    std::vector<double> secs(100);
    for (int r = 0; r < 100; r++) {
        uint8_t p[156];
        blankPatch(p);
        setOp(p, 0, 99, r, 0, 0, 99, 0, 0, 0, 99, 1, 2, 0, 0);   // 100 Hz
        // long enough for the slowest rates to finish
        const int blocks = (r < 20) ? 40000 : (r < 50 ? 8000 : 1200);
        secs[r] = decayOf(render(p, blocks));
    }
    for (int r = 0; r < 100; r += 5) {
        printf("   ");
        for (int k = r; k < r + 5 && k < 100; k++) printf(" %10.5ff,", secs[k]);
        printf("   // %d-%d\n", r, std::min(r + 4, 99));
    }
    printf("};\n");

    // ---- pitch envelope level -> semitones ----
    // L4 is the resting level; 50 is documented as no change. Hold every
    // stage at one level and measure the pitch it produces against the same
    // patch resting at 50.
    printf("\n// pitch EG level -> semitones from centre\n");
    {
        // Ratio mode, not fixed: a fixed-frequency operator ignores the pitch
        // envelope entirely, which is why the first attempt at this measured
        // zero everywhere. That behaviour is wanted for the noise layer - it
        // does not bend in the original either - but the drum body has to
        // track the envelope, so it has to be a ratio operator.
        auto pitchAt = [&](int lvl) {
            uint8_t p[156];
            blankPatch(p);
            setOp(p, 0, 99, 0, 0, 0, 99, 99, 99, 99, 99, 0, 1, 0, 0);  // ratio 1
            p[126] = 99; p[127] = 99; p[128] = 99; p[129] = 99;
            p[130] = lvl; p[131] = lvl; p[132] = lvl; p[133] = lvl;
            return pitchOf(render(p, 300));
        };
        const double ref = pitchAt(50);
        printf("//   level 50 rests at %.1f Hz\n", ref);
        const int lv[] = {0, 12, 25, 37, 50, 62, 75, 87, 99};
        printf("static const float kPitchEgSemis[9] = {   // at levels ");
        for (int i = 0; i < 9; i++) printf("%d%s", lv[i], i < 8 ? "," : "");
        printf("\n   ");
        for (int i = 0; i < 9; i++) {
            const double f = pitchAt(lv[i]);
            const double semis = (f > 1 && ref > 1) ? 12.0 * std::log2(f / ref) : 0.0;
            printf(" %8.3ff,", semis);
        }
        printf("\n};\n");
    }

    // a few landmarks, for the comment in the header
    printf("\n// landmarks: ");
    const int marks[] = {20, 40, 60, 80, 95, 99};
    for (int i = 0; i < 6; i++) printf("r%d=%.3fs  ", marks[i], secs[marks[i]]);
    printf("\n");
    return 0;
}
