// Maghreb Tubu - VST3 wrapper.
//
// An instrument, because Live hosts exactly two kinds of plug-in - things
// that take MIDI and make audio, and things that take audio and make audio.
// There is no MIDI-effect slot for plug-ins in any 12.x release, so a port of
// the Move module as a note effect would scan cleanly and then be unusable.
//
// Playing the tabʿ here rather than emitting notes for something else also
// removes the limitation the Move and Max for Live versions both have: they
// place neutral degrees with pitch bend, which is per-channel, so a chord can
// only carry one bend. This resolves the tuning to a frequency per voice.

#include "tubu_vst.h"
#include "tubu_editor.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <cstdio>
#include <cmath>

#define stringPluginName "Maghreb Tubu"
#define stringCompanyName "Wael"
#define stringCompanyWeb "https://example.invalid"
#define stringCompanyEmail "mailto:noreply@example.invalid"
#define FULL_VERSION_STR "0.1.0"

// Generated once and fixed: a host identifies the plug-in by this, so it must
// never change between builds or saved Sets will lose their device.
static const Steinberg::FUID kTubuUID(0x4C2A7E31, 0x8D6B4A02, 0xB15F9C74, 0x3E08D2A6);

namespace Steinberg {
namespace Vst {

static const char* const kMapNames[]   = {"Nearest", "Per key", "Per step"};
static const char* const kDroneNames[] = {"Off", "Tonic", "Tonic + 5th"};
static const char* const kDegNames[]   = {"1 tonic", "2 second", "3 third",
                                          "4 fourth", "5 fifth", "6 sixth",
                                          "7 seventh"};
static const char* const kRevertNames[]= {"-", "Reload factory tabʿ"};
#define NAMED(a) a, (int)(sizeof(a) / sizeof(a[0]))
#define ENUM nullptr, 0

const ParamDef kParams[] = {
//   name          lo     hi     def   steps names                  unit dispLo dispHi prec
    {"Tab",        0.0,  17.0,   3.0,  17,  tubu::kNames, tubu::kNumTubu,
                                                                    "",   0.0,  17.0,  0},
    // shown as a note name, not a MIDI number - see getParamStringByValue
    {"Tonic",     36.0,  84.0,  60.0,  48,  ENUM,                   "",  36.0,  84.0,  0},
    {"Mapping",    0.0,   2.0,   2.0,   2,  NAMED(kMapNames),       "",   0.0,   2.0,  0},
    {"Drone",      0.0,   2.0,   0.0,   2,  NAMED(kDroneNames),     "",   0.0,   2.0,  0},
    {"Drone Level",0.0,   1.0,   0.35, 0,   ENUM,                   "%",  0.0, 100.0,  0},
    // how fast the partials fall away from the fundamental
    {"Brightness", 0.2,   3.0,   1.2,  0,   ENUM,                   "",   0.2,   3.0,  2},
    // how much faster the upper partials decay than the lower ones
    {"Damping",    0.0,   2.0,   1.0,  0,   ENUM,                   "",   0.0,   2.0,  2},
    {"Decay",      0.05, 12.0,   2.5,  0,   ENUM,                   "s",  0.05, 12.0,  2},
    // 0 plucked, 100 sustained: the ʿūd end to the rabāb end
    {"Bow",        0.0,   1.0,   0.0,  0,   ENUM,                   "%",  0.0, 100.0,  0},
    {"Attack",     0.5, 500.0,   4.0,  0,   ENUM,                   "ms", 0.5, 500.0,  1},
    {"Release",    5.0,2000.0, 180.0,  0,   ENUM,                   "ms", 5.0,2000.0,  0},
    {"Level",      0.0,   1.0,   0.8,  0,   ENUM,                   "%",  0.0, 100.0,  0},
    {"Output",   -60.0,  12.0,   0.0,  0,   ENUM,                   "dB",-60.0, 12.0,  1},
    {"Degree",     0.0,   6.0,   1.0,  6,   NAMED(kDegNames),       "",   0.0,   6.0,  0},
    {"Cents",      0.0,1200.0, 150.0, 1200, ENUM,                   "c",  0.0,1200.0,  0},
    // wrapper-only
    {"Revert",     0.0,   1.0,   0.0,  1,   NAMED(kRevertNames),    "",   0.0,   1.0,  0},
};

const char* const kParamHelp[] = {
/*Tab*/        "Which of the eighteen ṭubūʿ is in force. Each is seven degrees in cents above the tonic; the ones that are not multiples of 100 are what 12-TET has no key for.",
/*Tonic*/      "The note the tabʿ is built on. Every degree is measured up from here.",
/*Mapping*/    "What a key means. Nearest pulls an incoming note to the closest degree, so an ordinary MIDI clip is bent into the tabʿ. Per key is the Move module's own, using the pitch class modulo seven. Per step gives one degree per key, so the keyboard rises evenly.",
/*Drone*/      "A held tonic, or tonic and fifth. The fifth is the tabʿ's own fifth - 650 cents in Mujannab, not a tempered 700.",
/*Drone Level*/"How loud the drone sits under the melody.",
/*Brightness*/ "How fast the partials fall away from the fundamental. Low is reedy and rich, high is close to a sine.",
/*Damping*/    "How much faster the upper partials decay than the lower ones. This is what makes a plucked note settle from bright to round.",
/*Decay*/      "How long a note rings once struck. Long values with Bow up give a sustained tone instead.",
/*Bow*/        "Plucked at 0, sustained at 100 - the ʿūd end to the rabāb end. It sets how much of each partial is held rather than allowed to decay.",
/*Attack*/     "How long a note takes to reach full level.",
/*Release*/    "How long a note takes to fade after you let go.",
/*Level*/      "Level of the voices, before Output.",
/*Output*/     "Master trim on everything the plug-in puts out.",
/*Degree*/     "Which degree the Cents control is editing. You can also drag a degree directly on the ruler.",
/*Cents*/      "Where the selected degree sits, in cents above the tonic. The written tables are one reading of a tradition that is not fixed to the cent, so they are meant to be moved.",
/*Revert*/     "Put the factory tabʿ back, discarding any degrees you have moved.",
};
static_assert(sizeof(kParamHelp) / sizeof(kParamHelp[0]) == kTotalParams,
              "every parameter needs a line of help");

static inline double denorm(int id, double n) {
    const ParamDef& p = kParams[id];
    double v = p.lo + n * (p.hi - p.lo);
    if (p.steps > 0) {
        const double step = (p.hi - p.lo) / p.steps;
        v = p.lo + std::floor((v - p.lo) / step + 0.5) * step;
    }
    return v;
}
static inline double norm(int id, double v) {
    const ParamDef& p = kParams[id];
    return (v - p.lo) / (p.hi - p.lo);
}

// Live writes middle C as C3, so MIDI 60 is C3 here too.
std::string TubuVST::noteName(int midi) {
    static const char* const n[12] = {"C", "C#", "D", "D#", "E", "F",
                                      "F#", "G", "G#", "A", "A#", "B"};
    char buf[16];
    snprintf(buf, sizeof buf, "%s%d", n[((midi % 12) + 12) % 12], midi / 12 - 2);
    return buf;
}

FUnknown* TubuVST::createInstance(void*) {
    return (IAudioProcessor*)new TubuVST();
}

tresult PLUGIN_API TubuVST::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addEventInput(STR16("MIDI In"), 16);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    for (int i : mplat::knobOrder(kKnobPages, kTotalParams)) {
        const ParamDef& p = kParams[i];
        char16 title[128];
        str8ToStr16(title, p.name, 128);

        if (p.names) {
            auto* sp = new StringListParameter(title, i);
            for (int k = 0; k < p.numNames; k++) {
                char16 v[128];
                str8ToStr16(v, p.names[k], 128);
                sp->appendString(v);
            }
            parameters.addParameter(sp);
            sp->getInfo().defaultNormalizedValue = norm(i, p.def);
            sp->setNormalized(norm(i, p.def));
        } else {
            char16 units[64];
            str8ToStr16(units, p.unit, 64);
            const double dispDef = p.dispLo + norm(i, p.def) * (p.dispHi - p.dispLo);
            auto* rp = new RangeParameter(title, i, p.unit[0] ? units : nullptr,
                                          p.dispLo, p.dispHi, dispDef, p.steps,
                                          ParameterInfo::kCanAutomate);
            rp->setPrecision(p.precision);
            parameters.addParameter(rp);
        }
        if (i < kNumDsp) {
            cache_[i] = (float)p.def;
            dsp_.setParam(i, (float)p.def);
        }
    }
    return kResultOk;
}

tresult PLUGIN_API TubuVST::terminate() {
    editor_ = nullptr;
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API TubuVST::setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                               SpeakerArrangement* out, int32 numOut) {
    if (numOut == 1 && out[0] == SpeakerArr::kStereo && numIn == 0)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

tresult PLUGIN_API TubuVST::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API TubuVST::setupProcessing(ProcessSetup& setup) {
    dsp_.setSampleRate(setup.sampleRate);
    for (int i = 0; i < kNumDsp; i++) dsp_.setParam(i, cache_[i]);
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API TubuVST::setActive(TBool state) {
    if (!state) dsp_.allNotesOff();
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API TubuVST::process(ProcessData& data) {
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
    if (data.numOutputs < 1 || data.outputs[0].numChannels < 2) return kResultOk;

    const int n = data.numSamples;
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];

    // Render in segments split at each note event, so a note lands on the
    // sample it was played on rather than at the top of the block.
    IEventList* events = data.inputEvents;
    const int32 numEvents = events ? events->getEventCount() : 0;
    int32 ei = 0;
    int pos = 0;

    while (pos < n) {
        int next = n;
        while (ei < numEvents) {
            Event e;
            if (events->getEvent(ei, e) != kResultOk) { ei++; continue; }
            int off = (int)std::max<int32>(0, std::min<int32>(e.sampleOffset, n));
            if (off > pos) { next = off; break; }

            if (e.type == Event::kNoteOnEvent) {
                if (e.noteOn.velocity > 0.f) dsp_.noteOn(e.noteOn.pitch, e.noteOn.velocity);
                else                          dsp_.noteOff(e.noteOn.pitch);
            } else if (e.type == Event::kNoteOffEvent) {
                dsp_.noteOff(e.noteOff.pitch);
            }
            ei++;
        }

        const int len = next - pos;
        if (len <= 0) break;
        dsp_.process(outL + pos, outR + pos, len);
        pos = next;
    }

    data.outputs[0].silenceFlags = 0;
    return kResultOk;
}

static const int32 kStateVersion = 1;

tresult PLUGIN_API TubuVST::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 version = 0, count = 0;
    if (!s.readInt32(version) || version != kStateVersion) return kResultFalse;
    if (!s.readInt32(count)) return kResultFalse;

    for (int i = 0; i < count; i++) {
        float v = 0;
        if (!s.readFloat(v)) return kResultFalse;
        if (i >= kNumDsp) continue;
        cache_[i] = v;
        dsp_.setParam(i, v);
        SingleComponentEffect::setParamNormalized(i, norm(i, v));
    }
    // the edited degrees, which are not parameters one by one
    for (int d = 0; d < tubu::kDegrees; d++) {
        int32 c = 0;
        if (!s.readInt32(c)) break;
        dsp_.setParam(tubu::kEditDegree, (float)d);
        dsp_.setParam(tubu::kEditCents, (float)c);
    }
    dsp_.setParam(tubu::kEditDegree, cache_[tubu::kEditDegree]);
    dsp_.setParam(tubu::kEditCents, cache_[tubu::kEditCents]);
    return kResultOk;
}

tresult PLUGIN_API TubuVST::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(kStateVersion);
    s.writeInt32(kNumDsp);
    for (int i = 0; i < kNumDsp; i++) s.writeFloat(cache_[i]);
    for (int d = 0; d < tubu::kDegrees; d++)
        s.writeInt32((int32)dsp_.degreeCents(d));
    return kResultOk;
}

IPlugView* PLUGIN_API TubuVST::createView(FIDString name) {
    if (name && strcmp(name, ViewType::kEditor) == 0)
        return new TubuEditor(this, this);
    return nullptr;
}

tresult PLUGIN_API TubuVST::getParamStringByValue(ParamID id, ParamValue v,
                                                  String128 str) {
    if ((int)id == tubu::kTonic) {
        const std::string s = noteName((int)(denorm(tubu::kTonic, v) + 0.5));
        str8ToStr16(str, s.c_str(), 128);
        return kResultOk;
    }
    return SingleComponentEffect::getParamStringByValue(id, v, str);
}

tresult PLUGIN_API TubuVST::setParamNormalized(ParamID id, ParamValue v) {
    tresult r = SingleComponentEffect::setParamNormalized(id, v);
    if (r != kResultOk) return r;

    if ((int)id == kRevertId) {
        const int idx = (int)(denorm(kRevertId, v) + 0.5);
        if (idx != revert_) {
            revert_ = idx;
            if (idx == 1) revertTuning();
        }
    }
    if (editor_) editor_->syncControl((int)id, v);
    return kResultOk;
}

//------------------------------------------------------------------------
void TubuVST::setParamFromUI(int id, double normalized) {
    if (id < 0 || id >= kTotalParams) return;
    applyParam((ParamID)id, normalized);
    SingleComponentEffect::setParamNormalized((ParamID)id, normalized);
    performEdit((ParamID)id, normalized);

    // Selecting a different degree has to pull its current cents into the
    // Cents control, or the control shows the previous degree's value and the
    // next nudge writes it onto the new one.
    if (id == tubu::kEditDegree) {
        const int d = (int)(denorm(tubu::kEditDegree, normalized) + 0.5);
        const double nv = norm(tubu::kEditCents, dsp_.degreeCents(d));
        cache_[tubu::kEditCents] = (float)dsp_.degreeCents(d);
        SingleComponentEffect::setParamNormalized(tubu::kEditCents, nv);
        performEdit(tubu::kEditCents, nv);
        if (editor_) editor_->syncControl(tubu::kEditCents, nv);
    }
}

void TubuVST::setDegreeFromUI(int degree, int cents, int phase) {
    if (degree < 0 || degree >= tubu::kDegrees) return;
    if (phase == 0) {
        beginEdit(tubu::kEditDegree);
        beginEdit(tubu::kEditCents);
        return;
    }
    if (phase == 2) {
        endEdit(tubu::kEditDegree);
        endEdit(tubu::kEditCents);
        return;
    }
    const double dn = norm(tubu::kEditDegree, degree);
    cache_[tubu::kEditDegree] = (float)degree;
    dsp_.setParam(tubu::kEditDegree, (float)degree);
    SingleComponentEffect::setParamNormalized(tubu::kEditDegree, dn);
    performEdit(tubu::kEditDegree, dn);

    const double cn = norm(tubu::kEditCents, cents);
    cache_[tubu::kEditCents] = (float)cents;
    dsp_.setParam(tubu::kEditCents, (float)cents);
    SingleComponentEffect::setParamNormalized(tubu::kEditCents, cn);
    performEdit(tubu::kEditCents, cn);

    if (editor_) {
        editor_->syncControl(tubu::kEditDegree, dn);
        editor_->syncControl(tubu::kEditCents, cn);
    }
}

void TubuVST::revertTuning() {
    int cents[tubu::kDegrees];
    dsp_.revertTuning(cents);

    const int d = (int)(cache_[tubu::kEditDegree] + 0.5f);
    const double nv = norm(tubu::kEditCents, cents[std::max(0, std::min(tubu::kDegrees - 1, d))]);
    cache_[tubu::kEditCents] = (float)cents[std::max(0, std::min(tubu::kDegrees - 1, d))];
    SingleComponentEffect::setParamNormalized(tubu::kEditCents, nv);
    beginEdit(tubu::kEditCents);
    performEdit(tubu::kEditCents, nv);
    endEdit(tubu::kEditCents);
    if (editor_) editor_->syncControl(tubu::kEditCents, nv);
}

void TubuVST::applyParam(ParamID id, ParamValue norm01) {
    if ((int)id < kNumDsp) {
        const float v = (float)denorm((int)id, norm01);
        cache_[id] = v;
        dsp_.setParam((int)id, v);
    } else if ((int)id == kRevertId) {
        const int idx = (int)(denorm(kRevertId, norm01) + 0.5);
        if (idx != revert_) { revert_ = idx; if (idx == 1) dsp_.revertTuning(nullptr); }
    }
}

} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(kTubuUID),
           Steinberg::PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Steinberg::Vst::kSimpleModeSupported,
           Steinberg::Vst::PlugType::kInstrumentSynth,
           FULL_VERSION_STR,
           kVstVersionString,
           Steinberg::Vst::TubuVST::createInstance)

END_FACTORY
