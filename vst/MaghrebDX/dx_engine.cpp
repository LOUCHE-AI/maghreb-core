// The only translation unit that sees msfa. Nothing from the VST3 SDK is
// included here, which is what keeps msfa's global min/max templates from
// colliding with std::max.

#include "dx_engine.h"

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

#include <cstring>
#include <cmath>
#include <vector>

namespace mdx {

static const int kVoices = 16;
static const int kStrokes = 11;

// Six operators summing land around 3.3 where the module's two-operator
// engine reaches 0.5, so the engine is trimmed here to sit in the same place
// before Level or Output ever see it.
static const float kEngineTrim = 0.16f;

struct DxEngine::Impl {
    struct Voice {
        std::unique_ptr<Dx7Note> note;
        bool active = false;
        int stroke = 0;
        int group = -1;
        float pan = 0.5f, gain = 1.f;
    };

    double sr = 44100.0;
    Voice voices[kVoices];
    int steal = 0;

    FmCore core;
    Controllers ctrls;
    std::shared_ptr<TuningState> tuning;

    float resL[N] = {0}, resR[N] = {0};
    int resAvail = 0;
    float peaks[kStrokes] = {0};

    void init(double rate) {
        sr = rate > 0 ? rate : 44100.0;
        Freqlut::init(sr);
        Sin::init();
        Exp2::init();
        Tanh::init();
        PitchEnv::init(sr);
        Env::init_sr(sr);
        Lfo::init(sr);
        if (!tuning) tuning = createStandardTuning();

        // Controllers' constructor sets six of its fields and leaves the rest
        // alone. That is invisible when the object is a global, because static
        // storage is zeroed first - which is how the offline tests and the
        // plug-in's first instance got away with it. Here it lives on the
        // heap, so every field it does not touch has to be set: a garbage
        // masterTune walks Dx7Note's pitch straight off the end of Freqlut's
        // table on the first note, and the process dies.
        ctrls.core = &core;
        for (int i = 0; i < 132; i++) ctrls.values_[i] = 0;
        ctrls.values_[kControllerPitch] = 0x2000;
        ctrls.modwheel_cc = ctrls.foot_cc = 0;
        ctrls.breath_cc = ctrls.aftertouch_cc = 0;
        ctrls.masterTune = 0;
        ctrls.portamento_enable_cc = false;
        ctrls.portamento_cc = 0;
        ctrls.portamento_gliss_cc = false;
        std::strcpy(ctrls.opSwitch, "111111");
        ctrls.refresh();

        for (int v = 0; v < kVoices; v++) {
            voices[v].note.reset(new Dx7Note(tuning, nullptr));
            voices[v].active = false;
        }
        resAvail = 0;
        for (int i = 0; i < kStrokes; i++) peaks[i] = 0.f;
    }

    void renderChunk() {
        for (int i = 0; i < N; i++) { resL[i] = 0.f; resR[i] = 0.f; }
        for (int vi = 0; vi < kVoices; vi++) {
            Voice& v = voices[vi];
            if (!v.active || !v.note) continue;

            int32_t buf[N] = {0};
            v.note->compute(buf, 0, 0, &ctrls);

            const float gl = (1.f - v.pan) * v.gain;
            const float gr = v.pan * v.gain;
            float loudest = 0.f;
            for (int i = 0; i < N; i++) {
                const float s = (float)(buf[i] / (double)(1 << 24));
                resL[i] += s * gl;
                resR[i] += s * gr;
                const float a = s < 0 ? -s : s;
                if (a > loudest) loudest = a;
            }
            if (v.stroke >= 0 && v.stroke < kStrokes) {
                const float lv = loudest * kEngineTrim;
                if (lv > peaks[v.stroke]) peaks[v.stroke] = lv;
            }
            if (!v.note->isPlaying()) v.active = false;
        }
        resAvail = N;
    }
};

DxEngine::DxEngine() : impl_(new Impl) { impl_->init(44100.0); }
DxEngine::~DxEngine() = default;

void DxEngine::setSampleRate(double sr) { impl_->init(sr); }

void DxEngine::noteOn(const uint8_t patch[156], int note, int velocity,
                      float pan, float gain, int stroke, int group) {
    // Release whatever this group was already holding. keyup rather than a
    // hard stop, so the previous note decays out instead of clicking off.
    if (group >= 0) {
        for (int v = 0; v < kVoices; v++) {
            Impl::Voice& x = impl_->voices[v];
            if (x.active && x.note && x.group == group) {
                x.note->keyup();
                x.group = -1;              // it is on its way out
            }
        }
    }

    int slot = -1;
    for (int v = 0; v < kVoices; v++)
        if (!impl_->voices[v].active) { slot = v; break; }
    if (slot < 0) slot = impl_->steal++ % kVoices;

    Impl::Voice& v = impl_->voices[slot];
    if (!v.note) return;
    v.note->init(patch, note, velocity, 1, &impl_->ctrls);
    v.active = true;
    v.stroke = stroke;
    v.group = group;
    v.pan = pan;
    v.gain = gain * kEngineTrim;
}

// A factory cartridge voice is a different animal from a patch derived from a
// stroke: six operators, often all of them carriers, held rather than struck.
// Measured over the 1088 voices in the folder here, the median peaks at 0.54
// and the loudest at 1.62 - so 65 of them clip before a level control is even
// touched, and a sustained one retriggered on a sixteenth grid stacks well
// past that. Hence this: the caller measures once, at the moment the patch is
// picked, and scales it.
float DxEngine::patchPeak(const uint8_t patch[156], int note, double seconds) const {
    if (!patch) return 0.f;
    Dx7Note probe(impl_->tuning, nullptr);
    probe.init(patch, note, 127, 1, &impl_->ctrls);
    const int blocks = (int)(seconds * impl_->sr / N);
    float pk = 0.f;
    for (int b = 0; b < blocks; b++) {
        int32_t buf[N] = {0};
        probe.compute(buf, 0, 0, &impl_->ctrls);
        for (int i = 0; i < N; i++) {
            const float s = (float)(buf[i] / (double)(1 << 24));
            const float a = s < 0 ? -s : s;
            if (a > pk) pk = a;
        }
    }
    return pk * kEngineTrim;
}

void DxEngine::allOff() {
    for (int v = 0; v < kVoices; v++) impl_->voices[v].active = false;
}

int DxEngine::activeVoices() const {
    int n = 0;
    for (int v = 0; v < kVoices; v++) if (impl_->voices[v].active) n++;
    return n;
}

void DxEngine::render(float* L, float* R, int n) {
    int done = 0;
    while (done < n) {
        if (impl_->resAvail == 0) impl_->renderChunk();
        int take = impl_->resAvail;
        if (take > n - done) take = n - done;
        const int from = N - impl_->resAvail;
        for (int i = 0; i < take; i++) {
            L[done + i] += impl_->resL[from + i];
            R[done + i] += impl_->resR[from + i];
        }
        impl_->resAvail -= take;
        done += take;
    }
}

void DxEngine::takeStrokePeaks(float* out, int count) {
    for (int i = 0; i < count && i < kStrokes; i++) {
        out[i] = impl_->peaks[i];
        impl_->peaks[i] = 0.f;
    }
}

} // namespace mdx
