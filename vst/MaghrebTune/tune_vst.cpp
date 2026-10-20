// Maghreb Tune - VST3 wrapper.

#include "tune_vst.h"
#include "tune_editor.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

#define stringPluginName "Maghreb Tune"
#define stringCompanyName "Wael"
#define stringCompanyWeb "https://example.invalid"
#define stringCompanyEmail "mailto:noreply@example.invalid"
#define FULL_VERSION_STR "0.1.0"

static const Steinberg::FUID kTuneUID(0x3C81D57A, 0x2E9F44B1, 0xA6035D8C, 0x71B2E940);

namespace Steinberg {
namespace Vst {

static inline double denorm(int id, double n) {
    const ParamDef& p = params()[id];
    double v = p.lo + n * (p.hi - p.lo);
    if (p.steps > 0) {
        const double step = (p.hi - p.lo) / p.steps;
        v = p.lo + std::floor((v - p.lo) / step + 0.5) * step;
    }
    return v;
}
static inline double norm(int id, double v) {
    const ParamDef& p = params()[id];
    return (v - p.lo) / (p.hi - p.lo);
}

FUnknown* TuneVST::createInstance(void*) { return (IAudioProcessor*)new TuneVST(); }

tresult PLUGIN_API TuneVST::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    const std::vector<ParamDef>& P = params();
    for (int i : mplat::knobOrder(kKnobPages, (int)P.size())) {
        const ParamDef& p = P[i];
        char16 title[128];
        str8ToStr16(title, p.name.c_str(), 128);

        if (p.names) {
            auto* sp = new StringListParameter(title, i);
            for (int k = 0; k < p.numNames; k++) {
                char16 v[256];
                str8ToStr16(v, p.names[k], 256);
                sp->appendString(v);
            }
            parameters.addParameter(sp);
            sp->getInfo().defaultNormalizedValue = norm(i, p.def);
            sp->setNormalized(norm(i, p.def));
        } else {
            char16 units[64];
            str8ToStr16(units, p.unit.c_str(), 64);
            const double dispDef = p.dispLo + norm(i, p.def) * (p.dispHi - p.dispLo);
            auto* rp = new RangeParameter(title, i,
                                          p.unit.empty() ? nullptr : units,
                                          p.dispLo, p.dispHi, dispDef, p.steps,
                                          ParameterInfo::kCanAutomate);
            rp->setPrecision(p.precision);
            parameters.addParameter(rp);
        }
        cache_[i] = (float)p.def;
        dsp_.setParam(i, (float)p.def);
    }
    return kResultOk;
}

tresult PLUGIN_API TuneVST::terminate() {
    editor_ = nullptr;
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API TuneVST::setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                               SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 && in[0] == out[0] &&
        (in[0] == SpeakerArr::kStereo || in[0] == SpeakerArr::kMono))
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

tresult PLUGIN_API TuneVST::canProcessSampleSize(int32 s) {
    return (s == kSample32) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API TuneVST::setupProcessing(ProcessSetup& setup) {
    dsp_.setSampleRate(setup.sampleRate);
    for (int i = 0; i < kNumDsp; i++) dsp_.setParam(i, cache_[i]);
    return SingleComponentEffect::setupProcessing(setup);
}

uint32 PLUGIN_API TuneVST::getLatencySamples() {
    // PSOLA needs a grain of lookahead; a host that is not told cannot line
    // the corrected vocal back up with the rest of the take.
    return (uint32)dsp_.latency();
}

tresult PLUGIN_API TuneVST::process(ProcessData& data) {
    if (data.inputParameterChanges) {
        int32 n = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < n; i++) {
            IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
            if (!q) continue;
            ParamValue v; int32 off;
            if (q->getPoint(q->getPointCount() - 1, off, v) != kResultTrue) continue;
            applyParam(q->getParameterId(), v);
        }
    }

    if (data.numSamples <= 0) return kResultOk;
    if (data.numInputs < 1 || data.numOutputs < 1) return kResultOk;

    const int32 chIn = data.inputs[0].numChannels;
    const int32 chOut = data.outputs[0].numChannels;
    if (chIn < 1 || chOut < 1) return kResultOk;

    float** in = data.inputs[0].channelBuffers32;
    float** out = data.outputs[0].channelBuffers32;

    if (chIn >= 2 && chOut >= 2)
        dsp_.processStereo(in[0], in[1], out[0], out[1], data.numSamples);
    else
        dsp_.process(in[0], out[0], data.numSamples);

    data.outputs[0].silenceFlags = 0;
    return kResultOk;
}

// 1 carries the parameters; 2 adds the edited degrees, because a tabʿ that has
// been nudged is part of the take, not a preference.
static const int32 kStateVersion = 2;

tresult PLUGIN_API TuneVST::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 version = 0, count = 0;
    if (!s.readInt32(version) || version < 1 || version > kStateVersion) return kResultFalse;
    if (!s.readInt32(count)) return kResultFalse;
    for (int i = 0; i < count; i++) {
        float v = 0;
        if (!s.readFloat(v)) return kResultFalse;
        if (i >= kNumDsp) continue;
        cache_[i] = v;
        dsp_.setParam(i, v);
        SingleComponentEffect::setParamNormalized(i, norm(i, v));
    }
    dsp_.revertTuning();
    edited_ = false;
    if (version >= 2) {
        for (int d = 0; d < tubu::kDegrees; d++) {
            int32 c = 0;
            if (!s.readInt32(c)) break;
            dsp_.setDegreeCents(d, c);
        }
        edited_ = true;
    }
    return kResultOk;
}

tresult PLUGIN_API TuneVST::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(kStateVersion);
    s.writeInt32(kNumDsp);
    for (int i = 0; i < kNumDsp; i++) s.writeFloat(cache_[i]);
    for (int d = 0; d < tubu::kDegrees; d++)
        s.writeInt32((int32)dsp_.degreeCents(d));
    return kResultOk;
}

IPlugView* PLUGIN_API TuneVST::createView(FIDString name) {
    if (name && strcmp(name, ViewType::kEditor) == 0) return new TuneEditor(this, this);
    return nullptr;
}

tresult PLUGIN_API TuneVST::setParamNormalized(ParamID id, ParamValue v) {
    tresult r = SingleComponentEffect::setParamNormalized(id, v);
    if (r == kResultOk && editor_) editor_->syncControl((int)id, v);
    // Live resizes the grain, and the grain is the latency. A host that is not
    // told goes on compensating by the figure it was given when the plug-in
    // loaded, and puts the corrected vocal back in the wrong place.
    if (r == kResultOk && (int)id == mtune::kLive && componentHandler)
        componentHandler->restartComponent(kLatencyChanged);
    return r;
}

void TuneVST::setParamFromUI(int id, double normalized) {
    if (id < 0 || id >= kTotalParams) return;
    applyParam((ParamID)id, normalized);
    SingleComponentEffect::setParamNormalized((ParamID)id, normalized);
    performEdit((ParamID)id, normalized);
}

void TuneVST::setDegreeFromUI(int degree, int cents) {
    dsp_.setDegreeCents(degree, cents);
    edited_ = true;
}

void TuneVST::applyPreset(int index) {
    if (index < 0 || index >= mtune::kNumPresets) return;
    const mtune::Preset& p = mtune::kPresets[index];
    setParamFromUI(mtune::kTab,       norm(mtune::kTab, p.tab));
    setParamFromUI(mtune::kTonic,     norm(mtune::kTonic, p.tonic));
    setParamFromUI(mtune::kTonicFine, norm(mtune::kTonicFine, 0.0));
    setParamFromUI(mtune::kAmount,    norm(mtune::kAmount, p.amount));
    setParamFromUI(mtune::kRetune,    norm(mtune::kRetune, p.retuneMs));
    setParamFromUI(mtune::kDeadzone,  norm(mtune::kDeadzone, p.deadCents));
    setParamFromUI(mtune::kMix,       norm(mtune::kMix, p.mix));
    setParamFromUI(mtune::kOutput,    norm(mtune::kOutput, 0.0));
    dsp_.revertTuning();
    edited_ = false;
    preset_ = index;
}

void TuneVST::revertTuning() {
    dsp_.revertTuning();
    edited_ = false;
}

void TuneVST::applyParam(ParamID id, ParamValue norm01) {
    if ((int)id >= kNumDsp) return;
    const float v = (float)denorm((int)id, norm01);
    cache_[id] = v;
    dsp_.setParam((int)id, v);
    // choosing a different tabʿ brings its own degrees with it
    if ((int)id == mtune::kTab) edited_ = false;
}

} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(kTuneUID),
           Steinberg::PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Steinberg::Vst::kSimpleModeSupported,
           Steinberg::Vst::PlugType::kFxPitchShift,
           FULL_VERSION_STR,
           kVstVersionString,
           Steinberg::Vst::TuneVST::createInstance)

END_FACTORY
