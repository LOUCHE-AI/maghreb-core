// Maghreb DX - VST3 wrapper.
//
// An instrument. Notes 36-46 strike the eleven strokes, the mapping the whole
// family uses, so Maghreb Seq's generated clips play this correctly with no
// remapping.

#include "dx_vst.h"
#include "dx_editor.h"
#include "kit_preset.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"
#include "platform.h"

#include <cstring>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <system_error>

#define stringPluginName "Maghreb DX"
#define stringCompanyName "Wael"
#define stringCompanyWeb "https://example.invalid"
#define stringCompanyEmail "mailto:noreply@example.invalid"
#define FULL_VERSION_STR "0.1.0"

static const Steinberg::FUID kDXUID(0x6B18F3A2, 0x74C24D95, 0xBE03A716, 0x5D8C24F0);

namespace Steinberg {
namespace Vst {

static const char* const kOffOn[] = {"Off", "On"};
static const char* const kKitNames[] = {"Bendir", "Darbuka", "Tar"};
static const char* const kResetNames[] = {"-", "Reset"};

// "36 D doum" and so on, so the selector reads like the module's
static const char* const* strokeLabels() {
    static std::vector<std::string> s;
    static std::vector<const char*> p;
    if (p.empty()) {
        for (int i = 0; i < mdx::kNumStrokes; i++) {
            char b[64];
            snprintf(b, sizeof b, "%d  %c  %s", mdx::kFirstNote + i,
                     mdx::kStrokeChars[i], mdx::kStrokeNames[i]);
            s.push_back(b);
        }
        for (auto& x : s) p.push_back(x.c_str());
    }
    return p.data();
}

const std::vector<ParamDef>& params() {
    static std::vector<ParamDef> P;
    if (!P.empty()) return P;
    P.resize(kTotalParams);

    auto set = [&](int id, const char* name, double lo, double hi, double def,
                   int steps, const char* const* names, int numNames,
                   const char* unit, double dLo, double dHi, int prec,
                   const char* help) {
        ParamDef& d = P[id];
        d.name = name; d.lo = lo; d.hi = hi; d.def = def; d.steps = steps;
        d.names = names; d.numNames = numNames; d.unit = unit;
        d.dispLo = dLo; d.dispHi = dHi; d.precision = prec; d.help = help;
    };

    set(mdx::kKit, "Kit", 0, 2, 0, 2, kKitNames, 3, "", 0, 2, 0,
        "Which drum the whole kit is. Each scales the engine's frequency, decay, noise and FM index.");
    set(mdx::kTune, "Tune", -24, 24, 0, 0, nullptr, 0, "st", -24, 24, 1,
        "Tunes every stroke together.");
    set(mdx::kDecay, "Decay", 0.25, 4, 1, 0, nullptr, 0, "x", 0.25, 4, 2,
        "Scales every envelope in the kit. Short is tight and dry, long rings.");
    set(mdx::kFM, "FM", 0, 2, 1, 0, nullptr, 0, "x", 0, 2, 2,
        "How hard the modulator drives the carrier. This is what turns a tone into a strike.");
    set(mdx::kNoise, "Noise", 0, 2, 1, 0, nullptr, 0, "x", 0, 2, 2,
        "Level of the band-passed noise layer - the skin and the rattle, as against the body.");
    set(mdx::kTone, "Tone", 0, 1, 0.5, 0, nullptr, 0, "%", 0, 100, 0,
        "Tilts the noise band up or down, so the same stroke reads as duller or brighter.");
    set(mdx::kDrive, "Drive", 0, 1, 0.12, 0, nullptr, 0, "%", 0, 100, 0,
        "The engine's own tanh stage. It thickens and compresses rather than distorting.");
    set(mdx::kLevel, "Level", 0, 1, 0.9, 0, nullptr, 0, "%", 0, 100, 0,
        "Level of the kit, before Output.");
    set(mdx::kHumanize, "Humanize", 0, 1, 0.3, 0, nullptr, 0, "%", 0, 100, 0,
        "Varies level, tuning and the noise seed from strike to strike. At zero two strikes are identical, which is what you want while editing a stroke.");
    set(mdx::kSpread, "Spread", 0, 1, 0.6, 0, nullptr, 0, "%", 0, 100, 0,
        "Widens the kit across the stereo field from each stroke's own pan.");
    set(mdx::kVelFM, "Vel > FM", 0, 1, 0.55, 0, nullptr, 0, "%", 0, 100, 0,
        "How much velocity opens the FM index. This is what makes a soft stroke read as softer rather than merely quieter.");
    set(mdx::kVelTone, "Vel > Tone", 0, 1, 0.45, 0, nullptr, 0, "%", 0, 100, 0,
        "How much velocity opens the noise layer.");
    set(mdx::kLowpass, "Lowpass", 200, 18000, 18000, 0, nullptr, 0, "Hz", 200, 18000, 0,
        "One-pole lowpass on the output, so a kit can sit under something else without another device.");
    set(mdx::kHighpass, "Highpass", 5, 4000, 5, 0, nullptr, 0, "Hz", 5, 4000, 0,
        "One-pole highpass on the output.");
    set(mdx::kOutput, "Output", -60, 12, 0, 0, nullptr, 0, "dB", -60, 12, 1,
        "Master trim. Can boost as well as cut.");

    set(mdx::kSelect, "Stroke", 0, mdx::kNumStrokes - 1, 0, mdx::kNumStrokes - 1,
        strokeLabels(), mdx::kNumStrokes, "", 0, mdx::kNumStrokes - 1, 0,
        "Which stroke the controls below are editing. Clicking a pad selects it and strikes it.");

    // the twelve numbers that make a stroke, plus its pan
    set(mdx::kSFreq, "Freq", 20, 8000, 78, 0, nullptr, 0, "Hz", 20, 8000, 1,
        "The carrier's pitch - the body of the drum.");
    set(mdx::kSRatio, "Ratio", 0.1, 16, 1.41, 0, nullptr, 0, "x", 0.1, 16, 2,
        "The modulator's frequency as a multiple of the carrier's. Whole numbers ring, everything else clangs.");
    set(mdx::kSIndex, "Index", 0, 8, 0.85, 0, nullptr, 0, "", 0, 8, 2,
        "How far the modulator swings the carrier. The attack lives here.");
    set(mdx::kSFmDec, "FM Time", 0.005, 4, 0.045, 0, nullptr, 0, "s", 0.005, 4, 3,
        "How fast the modulation falls away. Short makes a click on the front of a tone.");
    set(mdx::kSAmpDec, "Length", 0.005, 4, 0.42, 0, nullptr, 0, "s", 0.005, 4, 3,
        "How long the stroke rings. The kit-wide Decay scales this and every other time here.");
    set(mdx::kSBend, "Bend", 0, 4, 1.1, 0, nullptr, 0, "x", 0, 4, 2,
        "How far the pitch drops as the stroke sounds. This is what makes a drum a drum rather than a bell.");
    set(mdx::kSBendDec, "Bend Time", 0.005, 2, 0.03, 0, nullptr, 0, "s", 0.005, 2, 3,
        "How fast that pitch drop happens.");
    set(mdx::kSNoise, "Noise", 0, 2, 0.14, 0, nullptr, 0, "", 0, 2, 2,
        "Level of this stroke's own noise layer.");
    set(mdx::kSNoiseFreq, "Noise Freq", 60, 12000, 320, 0, nullptr, 0, "Hz", 60, 12000, 0,
        "Where the noise band sits.");
    set(mdx::kSNoiseQ, "Noise Q", 0.5, 6, 1.1, 0, nullptr, 0, "", 0.5, 6, 2,
        "How narrow that band is. Narrow rings, wide hisses.");
    set(mdx::kSNoiseDec, "Noise Time", 0.005, 2, 0.07, 0, nullptr, 0, "s", 0.005, 2, 3,
        "How long the noise lasts. Short is a stick, long is a rattle.");
    set(mdx::kSGain, "Gain", 0, 2, 1, 0, nullptr, 0, "x", 0, 2, 2,
        "This stroke's level against the others in the kit.");
    set(mdx::kSPan, "Pan", -1, 1, 0, 0, nullptr, 0, "", -1, 1, 2,
        "Where this stroke sits, before Spread widens it.");

    set(kResetOneId, "Reset Stroke", 0, 1, 0, 1, kResetNames, 2, "", 0, 1, 0,
        "Put this stroke back to the factory values.");
    set(kResetAllId, "Reset Kit", 0, 1, 0, 1, kResetNames, 2, "", 0, 1, 0,
        "Put every stroke back to the factory values.");
    return P;
}

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

FUnknown* DXVST::createInstance(void*) { return (IAudioProcessor*)new DXVST(); }

tresult PLUGIN_API DXVST::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addEventInput(STR16("MIDI In"), 16);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    const std::vector<ParamDef>& P = params();
    for (int i = 0; i < (int)P.size(); i++) {
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
            auto* rp = new RangeParameter(title, i, p.unit.empty() ? nullptr : units,
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

tresult PLUGIN_API DXVST::terminate() {
    editor_ = nullptr;
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API DXVST::setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                             SpeakerArrangement* out, int32 numOut) {
    if (numOut == 1 && out[0] == SpeakerArr::kStereo && numIn == 0)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

tresult PLUGIN_API DXVST::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API DXVST::setupProcessing(ProcessSetup& setup) {
    // setSampleRate resets the engine and its stroke table, so the table is
    // saved across and put back - otherwise changing the sample rate silently
    // discards every edit.
    std::vector<float> table(mdx::MaghrebDX::kTableFloats);
    dsp_.saveTable(table.data());
    dsp_.setSampleRate(setup.sampleRate);
    for (int i = 0; i < kNumDsp; i++) dsp_.setParam(i, cache_[i]);
    dsp_.loadTable(table.data());
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API DXVST::setActive(TBool state) {
    if (!state) dsp_.allNotesOff();
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API DXVST::process(ProcessData& data) {
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
            if (e.type == Event::kNoteOnEvent && e.noteOn.velocity > 0.f)
                dsp_.noteOn(e.noteOn.pitch, e.noteOn.velocity);
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

// The stroke table is not a list of parameters, so it is written after them.
// 2 adds the cartridge the browser was on. The imported voices were always
// saved, so a version 1 state already sounded right when it came back - it
// just could not tell you where the sounds had come from, which is what a
// plug-in reloaded after a crash needs most.
static const int32 kStateVersion = 2;

tresult PLUGIN_API DXVST::setState(IBStream* state) {
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
    int32 tableCount = 0;
    if (s.readInt32(tableCount) && tableCount == mdx::MaghrebDX::kTableFloats) {
        std::vector<float> t(tableCount);
        bool okAll = true;
        for (int i = 0; i < tableCount; i++) if (!s.readFloat(t[i])) { okAll = false; break; }
        if (okAll) dsp_.loadTable(t.data());
    }
    // imported voices, written after the table so an older state simply ends
    int32 importCount = 0;
    if (s.readInt32(importCount) && importCount == mdx::MaghrebDX::kImportFloats) {
        std::vector<float> im(importCount);
        bool okAll = true;
        for (int i = 0; i < importCount; i++) if (!s.readFloat(im[i])) { okAll = false; break; }
        if (okAll) dsp_.loadImported(im.data());
    }
    // The cartridge the browser was showing. Only its name is kept: the voices
    // themselves came back with the imported block above, so a cartridge that
    // has since moved costs the browser's position and nothing else.
    if (version >= 2) {
        int32 len = 0;
        if (s.readInt32(len) && len > 0 && len < 4096) {
            std::string path((size_t)len, '\0');
            if (s.readRaw(&path[0], len) == len) cartPath_ = std::move(path);
        }
    }

    pushSelectedToUI();
    return kResultOk;
}

tresult PLUGIN_API DXVST::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(kStateVersion);
    s.writeInt32(kNumDsp);
    for (int i = 0; i < kNumDsp; i++) s.writeFloat(cache_[i]);
    std::vector<float> t(mdx::MaghrebDX::kTableFloats);
    dsp_.saveTable(t.data());
    s.writeInt32((int32)t.size());
    for (size_t i = 0; i < t.size(); i++) s.writeFloat(t[i]);

    std::vector<float> im(mdx::MaghrebDX::kImportFloats);
    dsp_.saveImported(im.data());
    s.writeInt32((int32)im.size());
    for (size_t i = 0; i < im.size(); i++) s.writeFloat(im[i]);

    s.writeInt32((int32)cartPath_.size());
    if (!cartPath_.empty()) s.writeRaw(cartPath_.data(), (int32)cartPath_.size());
    return kResultOk;
}

IPlugView* PLUGIN_API DXVST::createView(FIDString name) {
    if (name && strcmp(name, ViewType::kEditor) == 0) return new DXEditor(this, this);
    return nullptr;
}

tresult PLUGIN_API DXVST::setParamNormalized(ParamID id, ParamValue v) {
    tresult r = SingleComponentEffect::setParamNormalized(id, v);
    if (r != kResultOk) return r;
    if ((int)id == kResetOneId && denorm(kResetOneId, v) >= 0.5) resetStroke(dsp_.selected());
    if ((int)id == kResetAllId && denorm(kResetAllId, v) >= 0.5) resetAll();
    if (editor_) editor_->syncControl((int)id, v);
    return kResultOk;
}

//------------------------------------------------------------------------
void DXVST::setParamFromUI(int id, double normalized) {
    if (id < 0 || id >= kTotalParams) return;
    applyParam((ParamID)id, normalized);
    SingleComponentEffect::setParamNormalized((ParamID)id, normalized);
    performEdit((ParamID)id, normalized);
    // Selecting a stroke has to pull its own numbers into the edit controls,
    // or they show the previous stroke's values and the next nudge writes
    // them onto this one.
    if (id == mdx::kSelect) pushSelectedToUI();
}

void DXVST::selectStroke(int stroke) {
    if (stroke < 0 || stroke >= mdx::kNumStrokes) return;
    setParamFromUI(mdx::kSelect, norm(mdx::kSelect, stroke));
}

void DXVST::resetStroke(int stroke) {
    dsp_.resetStroke(stroke);
    pushSelectedToUI();
}
void DXVST::resetAll() {
    dsp_.resetAll();
    pushSelectedToUI();
}

// Copies the selected stroke's twelve numbers into the edit parameters and
// tells the host and the editor about each one.
void DXVST::pushSelectedToUI() {
    float v[mdx::kNumStrokeParams + 1] = {0};
    dsp_.loadSelectedInto(v);
    for (int i = 0; i < mdx::kNumStrokeParams; i++) {
        const int id = mdx::kFirstStrokeParam + i;
        cache_[id] = v[i];
        const double nv = std::max(0.0, std::min(1.0, norm(id, v[i])));
        SingleComponentEffect::setParamNormalized(id, nv);
        if (editor_) editor_->syncControl(id, nv);
    }
}

std::string DXVST::kitFolder() {
    return mplat::userDataFolder("MaghrebDX", "Kits");
}

bool DXVST::pickCartVoice(const char* path, int voice, int stroke) {
    if (!path || !*path) return false;
    if (cartPath_ != path) {
        if (dsp_.loadCartridge(path) <= 0) return false;
        cartPath_ = path;
    }
    return dsp_.assignVoice(stroke, voice);
}

std::string DXVST::cartridgeFolder() {
    return mplat::userDataFolder("MaghrebDX", "Cartridges");
}

bool DXVST::saveKit(const char* path) {
    const std::vector<ParamDef>& P = params();
    std::vector<const char*> names;
    for (int i = 0; i < kNumDsp; i++) names.push_back(P[i].name.c_str());

    std::vector<float> table(mdx::MaghrebDX::kTableFloats);
    dsp_.saveTable(table.data());
    return kit::saveKit(path, names.data(), cache_, kNumDsp,
                        table.data(), mdx::kNumStrokes);
}

bool DXVST::loadKit(const char* path) {
    // The stroke rows arrive one at a time, so they are collected into a full
    // table and applied in one go - loadTable takes the whole thing, and a
    // half-written table would be heard on any strike in between.
    std::vector<float> table(mdx::MaghrebDX::kTableFloats);
    dsp_.saveTable(table.data());
    bool anyStroke = false;

    const bool okFile = kit::loadKit(path,
        [this](const std::string& name, double value) {
            const std::vector<ParamDef>& P = params();
            for (int i = 0; i < kNumDsp; i++) {
                if (name != P[i].name) continue;
                const double nv = std::max(0.0, std::min(1.0, norm(i, value)));
                cache_[i] = (float)denorm(i, nv);
                dsp_.setParam(i, cache_[i]);
                SingleComponentEffect::setParamNormalized(i, nv);
                beginEdit(i); performEdit(i, nv); endEdit(i);
                if (editor_) editor_->syncControl(i, nv);
                return;
            }
            // an unknown name is a kit from a build with a parameter this one
            // does not have; skipping it is the point of keying by name
        },
        [&](int stroke, const float* v) {
            if (stroke < 0 || stroke >= mdx::kNumStrokes) return;
            for (int k = 0; k < kit::kStrokeFields; k++)
                table[stroke * kit::kStrokeFields + k] = v[k];
            anyStroke = true;
        });

    if (!okFile) return false;
    if (anyStroke) dsp_.loadTable(table.data());
    pushSelectedToUI();
    return true;
}

void DXVST::applyParam(ParamID id, ParamValue norm01) {
    if ((int)id < kNumDsp) {
        const float v = (float)denorm((int)id, norm01);
        cache_[id] = v;
        dsp_.setParam((int)id, v);
    } else if ((int)id == kResetOneId) {
        if (denorm(kResetOneId, norm01) >= 0.5) dsp_.resetStroke(dsp_.selected());
    } else if ((int)id == kResetAllId) {
        if (denorm(kResetAllId, norm01) >= 0.5) dsp_.resetAll();
    }
}

} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(kDXUID),
           Steinberg::PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Steinberg::Vst::kSimpleModeSupported,
           Steinberg::Vst::PlugType::kInstrumentDrum,
           FULL_VERSION_STR,
           kVstVersionString,
           Steinberg::Vst::DXVST::createInstance)

END_FACTORY
