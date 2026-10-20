// Tubu DX - VST3 wrapper.
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

#include "tubudx_vst.h"
#include "tubudx_editor.h"
#include <filesystem>
#include <system_error>

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"
#include "platform.h"
#include "trigger_params.h"

#include <cstring>
#include <cstdio>
#include <cmath>

#define stringPluginName "Tubu DX"
#define stringCompanyName "Wael"
#define stringCompanyWeb "https://example.invalid"
#define stringCompanyEmail "mailto:noreply@example.invalid"
#define FULL_VERSION_STR "0.1.0"

// Generated once and fixed: a host identifies the plug-in by this, so it must
// never change between builds or saved Sets will lose their device.
static const Steinberg::FUID kTubuDXUID(0x1F5A93C8, 0x2E7B406D, 0x93C158AE, 0x7B240E96);

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
    {"Tonic",     36.0,  84.0,  60.0,  48,  ENUM,                   "",  36.0,  84.0,  0},
    {"Mapping",    0.0,   2.0,   2.0,   2,  NAMED(kMapNames),       "",   0.0,   2.0,  0},
    {"Degree",     0.0,   6.0,   1.0,   6,  NAMED(kDegNames),       "",   0.0,   6.0,  0},
    {"Cents",      0.0,1200.0, 150.0, 1200, ENUM,                   "c",  0.0,1200.0,  0},
    {"Drone",      0.0,   2.0,   0.0,   2,  NAMED(kDroneNames),     "",   0.0,   2.0,  0},
    {"Drone Level",0.0,   1.0,   0.35, 0,   ENUM,                   "%",  0.0, 100.0,  0},
    {"Voice",      0.0,  31.0,   0.0,  31,  ENUM,                   "",   0.0,  31.0,  0},
    {"Level",      0.0,   1.0,   0.8,  0,   ENUM,                   "%",  0.0, 100.0,  0},
    {"Output",   -60.0,  12.0,   0.0,  0,   ENUM,                   "dB",-60.0, 12.0,  1},
    {"Revert",     0.0,   1.0,   0.0,  1,   NAMED(kRevertNames),    "",   0.0,   1.0,  0},
};

const char* const kParamHelp[] = {
/*Tab*/        "Which of the eighteen Ṭubūʿ is in force. Each is seven degrees in cents above the tonic; the ones that are not multiples of 100 are what a keyboard has no key for.",
/*Tonic*/      "The note the tabʿ is built on. Every degree is measured up from here.",
/*Mapping*/    "What a key means. Nearest pulls an incoming note to the closest degree. Per key is the Move module's own. Per step gives one degree per key, so the keyboard rises evenly.",
/*Degree*/     "Which degree the Cents control is editing. You can also drag a degree on the ruler.",
/*Cents*/      "Where the selected degree sits, in cents above the tonic.",
/*Drone*/      "A held tonic, or tonic and fifth. The fifth is the tabʿ's own - 650 cents in Mujannab, not a tempered 700.",
/*Drone Level*/"How loud the drone sits under the melody.",
/*Voice*/      "Which voice of the loaded cartridge is played. With no cartridge loaded there is one built-in patch, kept plain so the tuning is what you hear.",
/*Level*/      "Level of the voices, before Output.",
/*Output*/     "Master trim. Can boost as well as cut.",
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
std::string TubuDXVST::noteName(int midi) {
    static const char* const n[12] = {"C", "C#", "D", "D#", "E", "F",
                                      "F#", "G", "G#", "A", "A#", "B"};
    char buf[16];
    snprintf(buf, sizeof buf, "%s%d", n[((midi % 12) + 12) % 12], midi / 12 - 2);
    return buf;
}

FUnknown* TubuDXVST::createInstance(void*) {
    return (IAudioProcessor*)new TubuDXVST();
}

tresult PLUGIN_API TubuDXVST::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addEventInput(STR16("MIDI In"), 16);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    const char* const* kTriggerNames = mdx::kPatchTriggerNames;
    const int kNumTriggers = mdx::kNumPatchTriggers;
    for (int i : mplat::knobOrder(kKnobPages, kFirstTrigger + kNumTriggers)) {
        if (i >= kFirstTrigger) {
            mplat::addTriggerParam(parameters, i, kTriggerNames[i - kFirstTrigger]);
            continue;
        }
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
    triggers_.start([this](int t) { runTrigger(t); });
    return kResultOk;
}

tresult PLUGIN_API TubuDXVST::terminate() {
    triggers_.stop();
    editor_ = nullptr;
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API TubuDXVST::setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                               SpeakerArrangement* out, int32 numOut) {
    if (numOut == 1 && out[0] == SpeakerArr::kStereo && numIn == 0)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

tresult PLUGIN_API TubuDXVST::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API TubuDXVST::setupProcessing(ProcessSetup& setup) {
    dsp_.setSampleRate(setup.sampleRate);
    for (int i = 0; i < kNumDsp; i++) dsp_.setParam(i, cache_[i]);
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API TubuDXVST::setActive(TBool state) {
    if (!state) dsp_.allNotesOff();
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API TubuDXVST::process(ProcessData& data) {
    if (data.inputParameterChanges) {
        int32 n = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < n; i++) {
            IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
            if (!q) continue;
            ParamValue v; int32 off;
            if (q->getPoint(q->getPointCount() - 1, off, v) != kResultTrue) continue;
            const int id = (int)q->getParameterId();
            if (id >= kFirstTrigger) triggers_.feed(id - kFirstTrigger, v);
            else applyParam((ParamID)id, v);
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

bool TubuDXVST::pickCartVoice(const char* path, int voice) {
    if (!path || !*path) return false;
    if (cartPath_ != path) {
        if (dsp_.loadCartridge(path) <= 0) return false;
        cartPath_ = path;
    }
    const int n = dsp_.cartridgeSize();
    if (n <= 0) return false;
    if (voice < 0) voice = 0;
    if (voice > n - 1) voice = n - 1;
    setParamFromUI(tdx::kVoice, norm(tdx::kVoice, voice));
    return true;
}

// ponytail: with the editor closed this runs on the trigger thread, and
// pickCartVoice tells the host through performEdit from there. Fine in the
// CLAP wrapper and in Bitwig; move the host notice to process() if a VST3
// host ever objects to hearing it off the UI thread.
std::string TubuDXVST::runTrigger(int t, const mdx::CartIndex* idx) {
    mdx::PatchRef p {cartPath_, currentVoice()};
    if (!mdx::patchForTrigger(t, p, cartridgeFolder(), mdx::favouritesFile("TubuDX"), idx))
        return mdx::noPatchMessage(t);
    if (!pickCartVoice(p.path.c_str(), p.voice)) return "That cartridge would not read.";
    return std::string("Patch: ") + voiceName(p.voice) + ".";
}

// 2 adds the cartridge path. A Set saved at version 1 loads with the built-in
// patch, which is what it had.
// 3 adds an edited patch. Bytes, not a file reference: there is no file for
// an edit to point at.
static const int32 kStateVersion = 3;

tresult PLUGIN_API TubuDXVST::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 version = 0, count = 0;
    if (!s.readInt32(version) || version < 1 || version > kStateVersion)
        return kResultFalse;
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
        dsp_.setParam(tdx::kEditDegree, (float)d);
        dsp_.setParam(tdx::kEditCents, (float)c);
    }
    dsp_.setParam(tdx::kEditDegree, cache_[tdx::kEditDegree]);
    dsp_.setParam(tdx::kEditCents, cache_[tdx::kEditCents]);

    // The cartridge is stored as a path, not as its 4 KB of bytes: the file is
    // shared with Maghreb DX and Maghreb Seq, and a Set carrying its own copy
    // would keep playing an old one after the cartridge folder was tidied.
    cartPath_.clear();
    if (version >= 2) {
        int32 len = 0;
        if (s.readInt32(len) && len > 0 && len < 4096) {
            std::string path((size_t)len, ' ');
            if (s.readRaw(&path[0], len) == len && dsp_.loadCartridge(path.c_str()) > 0)
                cartPath_ = path;
        }
    }
    dsp_.revertPatch();
    if (version >= 3) {
        int32 ed = 0;
        if (s.readInt32(ed) && ed) {
            uint8_t bytes[156];
            if (s.readRaw(bytes, 156) == 156) dsp_.loadEdited(bytes);
        }
    }
    return kResultOk;
}

tresult PLUGIN_API TubuDXVST::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(kStateVersion);
    s.writeInt32(kNumDsp);
    for (int i = 0; i < kNumDsp; i++) s.writeFloat(cache_[i]);
    for (int d = 0; d < tubu::kDegrees; d++)
        s.writeInt32((int32)dsp_.degreeCents(d));
    s.writeInt32((int32)cartPath_.size());
    if (!cartPath_.empty()) s.writeRaw(cartPath_.data(), (int32)cartPath_.size());
    s.writeInt32(dsp_.patchEdited() ? 1 : 0);
    if (dsp_.patchEdited()) s.writeRaw(dsp_.editedBytes(), 156);
    return kResultOk;
}

IPlugView* PLUGIN_API TubuDXVST::createView(FIDString name) {
    if (name && strcmp(name, ViewType::kEditor) == 0)
        return new TubuDXEditor(this, this);
    return nullptr;
}

tresult PLUGIN_API TubuDXVST::getParamStringByValue(ParamID id, ParamValue v,
                                                  String128 str) {
    if ((int)id == tdx::kTonic) {
        const std::string s = noteName((int)(denorm(tdx::kTonic, v) + 0.5));
        str8ToStr16(str, s.c_str(), 128);
        return kResultOk;
    }
    return SingleComponentEffect::getParamStringByValue(id, v, str);
}

tresult PLUGIN_API TubuDXVST::setParamNormalized(ParamID id, ParamValue v) {
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
void TubuDXVST::setParamFromUI(int id, double normalized) {
    if (id < 0 || id >= kTotalParams) return;
    applyParam((ParamID)id, normalized);
    SingleComponentEffect::setParamNormalized((ParamID)id, normalized);
    performEdit((ParamID)id, normalized);

    // Selecting a different degree has to pull its current cents into the
    // Cents control, or the control shows the previous degree's value and the
    // next nudge writes it onto the new one.
    if (id == tdx::kEditDegree) {
        const int d = (int)(denorm(tdx::kEditDegree, normalized) + 0.5);
        const double nv = norm(tdx::kEditCents, dsp_.degreeCents(d));
        cache_[tdx::kEditCents] = (float)dsp_.degreeCents(d);
        SingleComponentEffect::setParamNormalized(tdx::kEditCents, nv);
        performEdit(tdx::kEditCents, nv);
        if (editor_) editor_->syncControl(tdx::kEditCents, nv);
    }
}

void TubuDXVST::setDegreeFromUI(int degree, int cents, int phase) {
    if (degree < 0 || degree >= tubu::kDegrees) return;
    if (phase == 0) {
        beginEdit(tdx::kEditDegree);
        beginEdit(tdx::kEditCents);
        return;
    }
    if (phase == 2) {
        endEdit(tdx::kEditDegree);
        endEdit(tdx::kEditCents);
        return;
    }
    const double dn = norm(tdx::kEditDegree, degree);
    cache_[tdx::kEditDegree] = (float)degree;
    dsp_.setParam(tdx::kEditDegree, (float)degree);
    SingleComponentEffect::setParamNormalized(tdx::kEditDegree, dn);
    performEdit(tdx::kEditDegree, dn);

    const double cn = norm(tdx::kEditCents, cents);
    cache_[tdx::kEditCents] = (float)cents;
    dsp_.setParam(tdx::kEditCents, (float)cents);
    SingleComponentEffect::setParamNormalized(tdx::kEditCents, cn);
    performEdit(tdx::kEditCents, cn);

    if (editor_) {
        editor_->syncControl(tdx::kEditDegree, dn);
        editor_->syncControl(tdx::kEditCents, cn);
    }
}

void TubuDXVST::revertTuning() {
    int cents[tubu::kDegrees];
    dsp_.revertTuning(cents);

    const int d = (int)(cache_[tdx::kEditDegree] + 0.5f);
    const double nv = norm(tdx::kEditCents, cents[std::max(0, std::min(tubu::kDegrees - 1, d))]);
    cache_[tdx::kEditCents] = (float)cents[std::max(0, std::min(tubu::kDegrees - 1, d))];
    SingleComponentEffect::setParamNormalized(tdx::kEditCents, nv);
    beginEdit(tdx::kEditCents);
    performEdit(tdx::kEditCents, nv);
    endEdit(tdx::kEditCents);
    if (editor_) editor_->syncControl(tdx::kEditCents, nv);
}

std::string TubuDXVST::cartridgeFolder() {
    return mplat::userDataFolder("MaghrebDX", "Cartridges");
}

void TubuDXVST::applyParam(ParamID id, ParamValue norm01) {
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

DEF_CLASS2(INLINE_UID_FROM_FUID(kTubuDXUID),
           Steinberg::PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Steinberg::Vst::kSimpleModeSupported,
           Steinberg::Vst::PlugType::kInstrumentSynth,
           FULL_VERSION_STR,
           kVstVersionString,
           Steinberg::Vst::TubuDXVST::createInstance)

END_FACTORY
