// TapeRepair - VST3 wrapper.
//
// An effect that does not process in real time, which is unusual enough to say
// why: the repairs worth making to a tape transfer all need the whole
// recording. The hum notch runs forwards and backwards so it adds no phase
// shift; the hiss floor is measured from a stretch of this recording rather
// than guessed at from what has just gone past. Neither is possible three
// milliseconds at a time.
//
// So the device records a take, mends it when asked, and plays the result back
// into the track. What Bitwig has no device for is the mending.

#include "taperepair_vst.h"
#include "taperepair_editor.h"
#include "platform.h"
#include "handoff.h"
#include "wav_io.h"
#include "audio_import.h"

#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>

#define FULL_VERSION_STR "0.1.0"
#define stringPluginName "TapeRepair"
#define stringCompanyName "Wael\0"
#define stringCompanyWeb "https://example.invalid"
#define stringCompanyEmail "mailto:noreply@example.invalid"

static const Steinberg::FUID kTapeRepairUID(0x5E3B71A9, 0x4D8C42F6, 0xB0176C29, 0x8A4D31FB);

namespace Steinberg {
namespace Vst {

static const char* const kOffOn[]   = {"Off", "On"};
static const char* const kMains[]   = {"Off", "50 Hz", "60 Hz"};
static const char* const kCompareN[] = {"Repaired", "Off tape"};

// lo, hi, def are engine units; dispLo/dispHi are what a person reads.
const ParamDef kParams[] = {
    { "Hum",        0, 2, 1,  2, kMains,    3, "",   0,   2,   0 },
    { "Harmonics",  1, 8, 4,  7, nullptr,   0, "",   1,   8,   0 },
    { "Denoise",    0, 2, 1,  0, nullptr,   0, "x",  0,   2,   2 },
    { "Floor",    -40, 0, -18, 0, nullptr,  0, "dB", -40,  0,   0 },
    { "DC Block",   0, 1, 1,  1, kOffOn,    2, "",   0,   1,   0 },
    { "Normalise", -61, 0, -1, 0, nullptr,  0, "dB", -61,  0,   0 },
    { "Compare",    0, 1, 0,  1, kCompareN, 2, "",   0,   1,   0 },
    { "Output",   -24, 12, 0, 0, nullptr,   0, "dB", -24, 12,   1 },
    { "Bypass",     0, 1, 0,  1, kOffOn,    2, "",   0,   1,   0 },
    // the two that are not engine parameters
    { "Arm",        0, 1, 0,  1, kOffOn,    2, "",   0,   1,   0 },
    { "Play",       0, 1, 0,  1, kOffOn,    2, "",   0,   1,   0 },
    { "Follow",     0, 1, 0,  1, kOffOn,    2, "",   0,   1,   0 },
    { "Auto Stop",  0, 1, 0,  1, kOffOn,    2, "",   0,   1,   0 },
};

const char* const kParamHelp[] = {
    "Mains frequency to notch out, with its harmonics. 50 Hz in Europe and "
    "most of Africa and Asia; 60 in the Americas and Japan.",
    "How many multiples of the mains frequency to notch. Four reaches 200 Hz "
    "at 50, which is where hum stops being audible as hum.",
    "How much of the measured noise floor to subtract. Press LEARN first - "
    "with no profile this does nothing at all.",
    "The furthest any one band may be pulled down. This is what stops "
    "denoising sounding like a swimming pool: the hiss that is left stays "
    "steady instead of gating on and off around the music.",
    "Removes the offset a capture chain leaves on the waveform. Cheap, and it "
    "makes every level measured afterwards mean what it says.",
    "Peak the repair is scaled to. All the way down is off, which is what you "
    "want if this is going into a chain that expects the original level.",
    "Swaps what you hear between the repair and what came off the tape. The "
    "only honest way to judge any of this.",
    "Output trim.",
    "Passes the input through untouched, sample for sample.",
    "Records what comes in. Arming again starts a new take and drops the last.",
    "Plays the take back into the track. Whether that is the repair or the "
    "tape is what Compare decides.",
    "Arms with the host's transport: press play in the scene and the take "
    "starts, stop it and the take ends. Also how you get audio that is already "
    "on a track into here - play it at this.",
    "Ends the take by itself after ten seconds under -60 dB, and drops the "
    "run-out it just recorded, keeping a second so nothing is clipped. For "
    "playing a side in and walking away.",
};

// ---------------------------------------------------------------- helpers

static double norm(int id, double plain) {
    const ParamDef& p = kParams[id];
    return (p.hi > p.lo) ? (plain - p.lo) / (p.hi - p.lo) : 0.0;
}
static double plainOf(int id, double normalized) {
    const ParamDef& p = kParams[id];
    double v = p.lo + normalized * (p.hi - p.lo);
    if (p.steps > 0) v = std::round(v);
    return v;
}

FUnknown* TapeRepairVST::createInstance(void*) {
    return (IAudioProcessor*)new TapeRepairVST();
}

// ------------------------------------------------------------ lifecycle

tresult PLUGIN_API TapeRepairVST::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    for (int i = 0; i < kTotalParams; i++) {
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
            // The current value and the value the host resets to are different
            // things, and both have to be said.
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
        if (i < kNumDsp) dsp_.setParam(i, (float)p.def);
    }
    return kResultOk;
}

tresult PLUGIN_API TapeRepairVST::terminate() {
    editor_ = nullptr;
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API TapeRepairVST::setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                                     SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        in[0] == SpeakerArr::kStereo && out[0] == SpeakerArr::kStereo)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

tresult PLUGIN_API TapeRepairVST::canProcessSampleSize(int32 s) {
    return (s == kSample32) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API TapeRepairVST::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate;
    dsp_.setSampleRate(sampleRate_);
    return SingleComponentEffect::setupProcessing(setup);
}

// --------------------------------------------------------------- params

void TapeRepairVST::applyParam(int id, double normalized) {
    if (id < 0 || id >= kTotalParams) return;
    if (auto* p = parameters.getParameter(id)) p->setNormalized(normalized);

    const double v = plainOf(id, normalized);
    if (id < kNumDsp)      dsp_.setParam(id, (float)v);
    else if (id == kArmId)  dsp_.arm(v >= 0.5);
    else if (id == kPlayId) dsp_.play(v >= 0.5);
    else if (id == kFollowId)   dsp_.setFollowHost(v >= 0.5);
    else if (id == kAutoStopId) dsp_.setAutoStop(v >= 0.5);
}

void TapeRepairVST::setParamFromUI(int id, double normalized) {
    applyParam(id, normalized);
}

double TapeRepairVST::normalizedOf(int id) const {
    if (auto* p = parameters.getParameter(id)) return p->getNormalized();
    return 0.0;
}

// ---------------------------------------------------------------- audio

tresult PLUGIN_API TapeRepairVST::process(ProcessData& data) {
    if (data.inputParameterChanges) {
        const int32 n = data.inputParameterChanges->getParameterCount();
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

    // An unconnected input is silence, not a reason to read a null pointer.
    static std::vector<float> quiet;
    if ((int)quiet.size() < n) quiet.assign((size_t)n, 0.f);
    const float* inL = quiet.data();
    const float* inR = quiet.data();
    if (data.numInputs > 0 && data.inputs[0].numChannels >= 2) {
        inL = data.inputs[0].channelBuffers32[0];
        inR = data.inputs[0].channelBuffers32[1];
    }

    // What the host's own transport is doing. Not every host sends a context -
    // an effect running inside an editor has no timeline to report - so this
    // only ever adds a way to arm, never takes the button away.
    if (data.processContext)
        dsp_.setHostPlaying((data.processContext->state & ProcessContext::kPlaying) != 0);

    dsp_.processStereo(inL, inR, outL, outR, n);

    // Recording and playing both mean the transport moved, and the arm may
    // have released itself - at the end of the take, at the run-out, or
    // because the host stopped.
    if (!dsp_.playing() && normalizedOf(kPlayId) > 0.5)
        applyParam(kPlayId, 0.0);
    if (dsp_.armed() != (normalizedOf(kArmId) > 0.5))
        applyParam(kArmId, dsp_.armed() ? 1.0 : 0.0);

    return kResultOk;
}

// ---------------------------------------------------------------- state

tresult PLUGIN_API TapeRepairVST::getState(IBStream* state) {
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(1);                                  // version
    for (int i = 0; i < kTotalParams; i++) s.writeDouble(normalizedOf(i));
    // The take itself is not written. A side of tape is hundreds of megabytes
    // and a project file is not where that belongs - archive it with tapedeck
    // and load the result, which is what the two of them are for.
    return kResultOk;
}

tresult PLUGIN_API TapeRepairVST::setState(IBStream* state) {
    IBStreamer s(state, kLittleEndian);
    int32 version = 0;
    if (!s.readInt32(version)) return kResultFalse;
    for (int i = 0; i < kTotalParams; i++) {
        double v = 0;
        if (!s.readDouble(v)) break;
        applyParam(i, v);
    }
    return kResultOk;
}

// -------------------------------------------------------------- actions

bool TapeRepairVST::loadFile(const std::string& path, std::string* err) {
    std::vector<std::vector<float>> channels;
    if (!maudio::importAudio(path, channels, sampleRate_, err)) return false;
    if (!dsp_.setTake(std::move(channels))) {
        if (err) *err = "that file has no audio in it";
        return false;
    }
    // A loaded file is not being recorded, whatever the arm button thinks.
    applyParam(kArmId, 0.0);
    applyParam(kPlayId, 0.0);
    return true;
}

bool TapeRepairVST::learn()  { return dsp_.learn(); }
// Started, not finished - see the engine's header for what pressing this used
// to do to the host. The editor's timer watches it from here.
bool TapeRepairVST::repair() { return dsp_.startRepair(); }
void TapeRepairVST::revert() { dsp_.revert(); }

std::string TapeRepairVST::sendToTapeGrain(std::string* err) {
    const trestore::Clip& src = dsp_.hasRepair() ? dsp_.repaired() : dsp_.take();
    if (src.ch.empty() || src.ch[0].empty()) {
        if (err) *err = "nothing recorded yet";
        return {};
    }

    // The folder and the name are the whole of the agreement between the two
    // plug-ins; shared/handoff.h holds it so neither can drift from it.
    const std::string folder = mhand::folder();

    char stamp[32];
    const std::tm tm = mplat::localTime(std::time(nullptr));
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H%M%S", &tm);

    const std::string name = std::string(mhand::prefix()) + stamp +
                             (dsp_.hasRepair() ? " (repaired).wav" : " (off tape).wav");
    const std::string path = (std::filesystem::path(folder) / name).string();

    if (!mwav::writeWav24(path, src.ch, (int)sampleRate_)) {
        if (err) *err = "could not write " + path;
        return {};
    }
    return path;
}

int TapeRepairVST::splitToFiles(std::string* err) {
    const trestore::Clip& src = dsp_.hasRepair() ? dsp_.repaired() : dsp_.take();
    if (src.ch.empty() || src.ch[0].empty()) {
        if (err) *err = "nothing recorded yet";
        return 0;
    }

    const std::vector<trestore::Track> tracks = dsp_.tracks();
    if (tracks.empty()) {
        if (err) *err = "no songs found on this side";
        return 0;
    }

    const std::string folder = mhand::folder();
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);

    char stamp[32];
    const std::tm tm = mplat::localTime(std::time(nullptr));
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H%M%S", &tm);

    int written = 0;
    for (size_t i = 0; i < tracks.size(); i++) {
        const trestore::Clip one = trestore::extract(src, tracks[i]);
        if (one.ch.empty() || one.ch[0].empty()) continue;

        char name[160];
        snprintf(name, sizeof name, "%s%s (track %zu of %zu)%s.wav",
                 mhand::prefix(), stamp, i + 1, tracks.size(),
                 dsp_.hasRepair() ? " repaired" : "");
        const std::string path = (std::filesystem::path(folder) / name).string();
        if (mwav::writeWav24(path, one.ch, (int)sampleRate_)) written++;
    }

    if (written == 0 && err) *err = "could not write into " + folder;
    return written;
}

IPlugView* PLUGIN_API TapeRepairVST::createView(FIDString name) {
    if (name && strcmp(name, ViewType::kEditor) == 0)
        return new TapeRepairEditor(this, this);
    return nullptr;
}

} // namespace Vst
} // namespace Steinberg

// InitModule/DeinitModule are not defined here: with VSTGUI support enabled
// the SDK supplies them itself, and they are what run VSTGUI's own
// initialisation. Defining them again is a duplicate-symbol link error.

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(kTapeRepairUID),
           Steinberg::PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Steinberg::Vst::kSimpleModeSupported,
           // an effect, and specifically a restoration one
           Steinberg::Vst::PlugType::kFxRestoration,
           FULL_VERSION_STR,
           kVstVersionString,
           Steinberg::Vst::TapeRepairVST::createInstance)

END_FACTORY
