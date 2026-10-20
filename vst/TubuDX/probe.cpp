#include "tubudx_dsp.h"
#include <cstdio>
int main() {
    printf("1 construct\n"); fflush(stdout);
    tdx::TubuDX g;
    printf("2 setSampleRate\n"); fflush(stdout);
    g.setSampleRate(48000);
    printf("3 noteOn\n"); fflush(stdout);
    g.noteOn(60, 1.f);
    printf("4 process\n"); fflush(stdout);
    float l[256], r[256];
    g.process(l, r, 256);
    printf("5 done, voices=%d\n", g.activeVoices()); fflush(stdout);
    return 0;
}
