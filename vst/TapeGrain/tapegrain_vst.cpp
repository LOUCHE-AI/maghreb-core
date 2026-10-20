// TapeGrain - VST3 wrapper.
//
// The engine lives in tapegrain_dsp.h and knows nothing about VST3; this file
// only maps parameters, notes and buffers. That separation is what lets the
// audio be rendered and checked offline before it ever reaches a host.
//
// It registers as an instrument with an event input, because that is what it
// is: notes play the grain cloud, and it is silent at rest. The audio input
// bus is auxiliary rather than main, which is how a host on a MIDI track can
// still route something into Record and Input Mix.

#include "tapegrain_vst.h"
#include "tapegrain_editor.h"
#include "tapegrain_io.h"
#include "tapegrain_preset.h"
#include "tapegrain_stems.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"
#include "platform.h"
#include "handoff.h"
#include "trigger_params.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <system_error>

#define stringPluginName "TapeGrain"
#define stringCompanyName "Wael"
#define stringCompanyWeb "https://example.invalid"
#define stringCompanyEmail "mailto:noreply@example.invalid"
#define FULL_VERSION_STR "0.2.0"

// Generated once and fixed: a host identifies the plug-in by this, so it must
// never change between builds or saved Sets will lose their device.
static const Steinberg::FUID kTapeGrainUID(0x7A9E1C40, 0x6B2E4F11, 0x9C3D5A87, 0x21B4E6D9);

namespace Steinberg {
namespace Vst {

// Value names, so the host shows "PingPong" rather than 2.
static const char* const kFocusNames[]   = {"Full", "Low", "Mid", "High"};
static const char* const kMotionNames[]  = {"Static", "Scan", "PingPong", "Walk",
                                            "Step", "Similar", "Swarm", "PitchSwarm"};
static const char* const kPadNames[]     = {"Pitch", "Spread", "Slice", "Both"};
static const char* const kVoicingNames[] = {"Mono", "Poly"};
static const char* const kSyncNames[]    = {"Off", "Internal", "Host"};
static const char* const kRateNames[]    = {"1/2", "1/4", "1/8", "1/8T", "1/16", "1/32"};
static const char* const kOffOnNames[]   = {"Off", "On"};
static const char* const kPresetNames[]  = {"--", "Tape Loop", "Glitch",
                                            "Pulse Bed", "Cloud Pad", "Spatial Drift"};
static const char* const kStemNames[]    = {"A", "B", "C", "D"};
#define NAMED(a) a, (int)(sizeof(a) / sizeof(a[0]))

#define ENUM nullptr, 0
const ParamDef kParams[] = {
//   name            lo      hi     def  steps  names                   unit    dispLo dispHi prec
    // a fraction of the buffer: people think in percent, not in 0.25
    {"Position",     0.0,   1.0,   0.25, 0,  ENUM,                      "%",    0.0,   100.,  1},
    // up to ten seconds, on a cubed curve so short grains keep their travel:
    // 180 ms sits a quarter of the way up and the old 2 s ceiling past half
    {"Grain Size",   5.0,  10000., 180., 0,  ENUM,                      "ms",   5.0,  10000., 0, 3.0},
    // grains per second, which is what a rate in Hz means here
    {"Density",      0.5,   150.,  30.,  0,  ENUM,                      "Hz",   0.5,   150.,  1},
    // whole semitones: anything finer is Pitch Spread's job
    {"Pitch",       -24.,   24.,   0.,   48, ENUM,                      "st",  -24.,   24.,   0},
    {"Level",        0.0,   1.0,   0.8,  0,  ENUM,                      "%",    0.0,   100.,  0},
    {"Grain Shape",  0.0,   1.0,   0.5,  0,  ENUM,                      "%",    0.0,   100.,  0},
    {"Attack",       1.0,   4000., 40.,  0,  ENUM,                      "ms",   1.0,   4000., 0},
    {"Release",      1.0,   8000., 600., 0,  ENUM,                      "ms",   1.0,   8000., 0},
    {"Spray",        0.0,   2000., 120., 0,  ENUM,                      "ms",   0.0,   2000., 0},
    {"Jitter",       0.0,   1.0,   0.0,  0,  ENUM,                      "%",    0.0,   100.,  0},
    // already a percentage of grains that play backwards
    {"Reverse",      0.0,   100.,  0.0,  0,  ENUM,                      "%",    0.0,   100.,  0},
    {"Pan Spray",    0.0,   1.0,   0.7,  0,  ENUM,                      "%",    0.0,   100.,  0},
    {"Focus",        0.0,   3.0,   0.0,  3,  NAMED(kFocusNames),        "",     0.0,   3.0,   0},
    {"Motion",       0.0,   7.0,   0.0,  7,  NAMED(kMotionNames),       "",     0.0,   7.0,   0},
    // a speed against realtime, so x reads better than a bare number
    {"Scan Rate",   -4.0,   4.0,   1.0,  0,  ENUM,                      "x",   -4.0,   4.0,   2},
    {"Span",         0.005, 1.0,   0.25, 0,  ENUM,                      "%",    0.5,   100.,  1},
    // only in play when Sync is Off; otherwise Tempo and Rate decide
    {"Step Rate",    0.1,   32.,   4.0,  0,  ENUM,                      "Hz",   0.1,   32.,   1},
    {"Affinity",     0.0,   1.0,   0.3,  0,  ENUM,                      "%",    0.0,   100.,  0},
    {"Cohesion",     0.0,   1.0,   0.4,  0,  ENUM,                      "%",    0.0,   100.,  0},
    {"Drift",        0.0,   1.0,   0.3,  0,  ENUM,                      "%",    0.0,   100.,  0},
    // a width in semitones, half-steps are musically useful here
    {"Pitch Spread", 0.0,   24.,   7.0,  0,  ENUM,                      "st",   0.0,   24.,   1},
    {"Freeze",       0.0,   1.0,   0.0,  1,  NAMED(kOffOnNames),        "",     0.0,   1.0,   0},
    // 0 is grains only, 1 is the untouched input - the module's own meaning
    {"Input Mix",    0.0,   1.0,   0.0,  0,  ENUM,                      "%",    0.0,   100.,  0},
    {"Pad Mode",     0.0,   3.0,   0.0,  3,  NAMED(kPadNames),          "",     0.0,   3.0,   0},
    {"Sensitivity",  0.0,   1.0,   0.5,  0,  ENUM,                      "%",    0.0,   100.,  0},
    // a count of keys, so it steps in whole keys
    {"Spread Keys",  2.0,   64.,   16.,  62, ENUM,                      "keys", 2.0,   64.,   0},
    {"Voicing",      0.0,   1.0,   1.0,  1,  NAMED(kVoicingNames),      "",     0.0,   1.0,   0},
    {"Sync",         0.0,   2.0,   0.0,  2,  NAMED(kSyncNames),         "",     0.0,   2.0,   0},
    // whole BPM, matching how a tempo is written down
    {"Tempo",        20.,   300.,  110., 280,ENUM,                      "BPM",  20.,   300.,  0},
    {"Rate",         0.0,   5.0,   4.0,  5,  NAMED(kRateNames),         "",     0.0,   5.0,   0},
    // a count of things in the flock: only whole agents exist
    {"Agents",       2.0,   24.,   12.,  22, ENUM,                      "",     2.0,   24.,   0},
    // master trim, in dB: Level only attenuates the grains, this can make up
    // gain as well, which a quiet tape capture usually needs
    {"Output",     -60.0,   12.,   0.0,  0,  ENUM,                      "dB",  -60.0,  12.,   1},
    // which of the four loaded samples is playing - switchable mid-note
    {"Stem",         0.0,   3.0,   0.0,  3,  NAMED(kStemNames),         "",     0.0,   3.0,   0},
    // how many grains may be in flight at once
    {"Grains",       4.0,   256.,  48.,  252,ENUM,                      "",     4.0,   256.,  0},
    // the stem mixer. All four are read at the same playhead and summed, so
    // with everything up you get the original track back.
    {"Stem A",       0.0,   1.0,   1.0,  0,  ENUM,                      "%",    0.0,   100.,  0},
    {"Stem B",       0.0,   1.0,   0.0,  0,  ENUM,                      "%",    0.0,   100.,  0},
    {"Stem C",       0.0,   1.0,   0.0,  0,  ENUM,                      "%",    0.0,   100.,  0},
    {"Stem D",       0.0,   1.0,   0.0,  0,  ENUM,                      "%",    0.0,   100.,  0},
    // wrapper-only, after the engine's own parameters
    {"Record",       0.0,   1.0,   0.0,  1,  NAMED(kOffOnNames),        "",     0.0,   1.0,   0},
    {"Preset",       0.0,   5.0,   0.0,  5,  NAMED(kPresetNames),       "",     0.0,   5.0,   0},
};
#undef ENUM

const char* const kParamHelp[] = {
/*Position*/    "Where in the sample grains are taken from. Every motion mode travels out from here. Drag the waveform to set it.",
/*Grain Size*/  "Length of each grain, 5 ms to 10 s. Short is granular texture; long behaves more like a loop.",
/*Density*/     "How many grains start each second. High values overlap into a continuous cloud.",
/*Pitch*/       "Transposes the grains. Independent of Scan Rate, so pitch and travel speed stay separate.",
/*Level*/       "Level of the grain cloud itself, before Output.",
/*Grain Shape*/ "The window of each grain: 0 a soft centred swell, 50 Hann, 100 instant attack and decay. Latched when a grain is born, so a cloud morphs gradually.",
/*Attack*/      "How long a note takes to reach full level. This is the voice envelope, not the grain.",
/*Release*/     "How long a note takes to fade once you let go. Its grains keep ringing until they finish.",
/*Spray*/       "Scatters where each grain is taken from, around Position.",
/*Jitter*/      "Randomises the gap between grains, so a dense cloud stops sounding metronomic.",
/*Reverse*/     "Share of grains that play backwards.",
/*Pan Spray*/   "How far grains scatter across the stereo field.",
/*Focus*/       "Grains only the low, mid or high band of the sample. A rough way to pull out a bassline or hats - not real separation.",
/*Motion*/      "How the playhead travels. Static sits still; Scan and PingPong sweep; Walk drifts; Step jumps between detected onsets; Similar jumps to whatever sounds alike; Swarm and PitchSwarm are flocks.",
/*Scan Rate*/   "Speed the playhead travels. 1.00x is realtime, negative runs backwards, 0 freezes. Used by Scan and PingPong.",
/*Span*/        "How much of the sample the motion covers, centred on Position. This is the shaded band on the waveform - drag its edges.",
/*Step Rate*/   "Steps per second for Step and Similar motion. Ignored unless Sync is Off.",
/*Affinity*/    "In Similar, how loosely a matching frame is chosen: 0 always takes the closest match, higher wanders. In the swarm modes, the pull back toward Position.",
/*Cohesion*/    "How tightly the swarm agents cluster. High is a single travelling knot; low spreads them across the buffer.",
/*Drift*/       "How far the swarm centre wanders, and how far Walk strays from Position.",
/*Pitch Spread*/"Width of the pitch flock in PitchSwarm. No effect in the other modes.",
/*Freeze*/      "Holds every playhead and the swarm exactly where they are. Grains keep sounding.",
/*Input Mix*/   "Blends the audio input in. 0 is grains only, 100 is the input untouched. On a MIDI track this needs a sidechain routing.",
/*Pad Mode*/    "What a note means. Pitch transposes the cloud; Spread lays the sample across the keys at original pitch; Slice jumps to a detected onset; Both slices and transposes.",
/*Sensitivity*/ "How readily onsets are detected. Low finds only the loud hits, high picks up quiet ones too. The ticks on the waveform are what it found.",
/*Spread Keys*/ "How many keys the sample is divided across in Spread mode. Ignored in the other pad modes.",
/*Voicing*/     "Mono plays one note at a time; Poly holds up to six, each with its own playhead and envelope.",
/*Sync*/        "Off runs free. Internal retriggers the playhead to Position on every step at its own Tempo; Host does the same from the DAW's tempo.",
/*Tempo*/       "Tempo used when Sync is Internal. Ignored when Sync is Off or Host.",
/*Rate*/        "Step division when Sync is on. Ignored when Sync is Off.",
/*Agents*/      "How many agents are in the swarm. A handful reads as separate voices, two dozen as a cloud. Only used by Swarm and PitchSwarm.",
/*Output*/      "Master trim on everything the plug-in puts out, input included. Unlike Level it can boost as well as cut.",
/*Stem*/        "Which slot the waveform shows and Load Sample writes to. This is a view, not what you hear - the four Stem levels decide that.",
/*Grains*/      "How many grains may sound at once. Low counts read as separate particles, high counts as a solid mass. Density and Grain Size decide how many are actually asked for.",
/*Stem A*/      "Level of stem A in the grain source. All four are read at the same playhead, so with everything up you granulate the whole track and pulling one down remixes it live.",
/*Stem B*/      "Level of stem B in the grain source.",
/*Stem C*/      "Level of stem C in the grain source.",
/*Stem D*/      "Level of stem D in the grain source.",
/*Record*/      "Captures the audio input into the buffer, replacing the sample. Turn it off to keep the take and re-slice it.",
/*Preset*/      "Five starting points, not modes. Recalling one leaves your sample, position, pitch, level and tempo alone.",
};
static_assert(sizeof(kParamHelp) / sizeof(kParamHelp[0]) == kTotalParams,
              "every parameter needs a line of help");

static inline double curveOut(const ParamDef& p, double n) {
    n = std::max(0.0, std::min(1.0, n));
    return p.curve == 1.0 ? n : std::pow(n, p.curve);
}
static inline double curveIn(const ParamDef& p, double t) {
    t = std::max(0.0, std::min(1.0, t));
    return p.curve == 1.0 ? t : std::pow(t, 1.0 / p.curve);
}

// The host's view of a curved parameter. RangeParameter maps linearly, so
// without this Live would show 2500 ms where the engine is playing 400.
class CurvedRangeParameter : public RangeParameter {
public:
    CurvedRangeParameter(const TChar* title, ParamID tag, const TChar* units,
                         ParamValue lo, ParamValue hi, ParamValue def,
                         int32 flags, const ParamDef& d)
        : RangeParameter(title, tag, units, lo, hi, def, 0, flags), d_(d) {
        // the base constructor normalised the default before this class
        // existed, i.e. linearly - redo it on the curve
        info.defaultNormalizedValue = toNormalized(def);
        setNormalized(info.defaultNormalizedValue);
    }
    ParamValue toPlain(ParamValue n) const SMTG_OVERRIDE {
        return getMin() + curveOut(d_, n) * (getMax() - getMin());
    }
    ParamValue toNormalized(ParamValue v) const SMTG_OVERRIDE {
        const double span = getMax() - getMin();
        return span > 0 ? curveIn(d_, (v - getMin()) / span) : 0.0;
    }
private:
    const ParamDef& d_;
};

static inline double denorm(int id, double n) {
    const ParamDef& p = kParams[id];
    double v = p.lo + curveOut(p, n) * (p.hi - p.lo);
    if (p.steps > 0) {
        // Snap to the value the host is displaying. Without this a stepped
        // parameter reads as one thing and reaches the engine as another -
        // Pitch showing 0 st while playing a third of a semitone sharp.
        const double stepSize = (p.hi - p.lo) / p.steps;
        v = p.lo + std::floor((v - p.lo) / stepSize + 0.5) * stepSize;
    }
    return v;
}
static inline double norm(int id, double v) {
    const ParamDef& p = kParams[id];
    return curveIn(p, (v - p.lo) / (p.hi - p.lo));
}

FUnknown* TapeGrainVST::createInstance(void*) {
    return (IAudioProcessor*)new TapeGrainVST();
}

tresult PLUGIN_API TapeGrainVST::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addEventInput(STR16("MIDI In"), 16);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    // One per stem slot, so a separated track can go four ways at once
    // instead of being flattened again on the way out. Auxiliary rather than
    // main: a host that ignores them sees exactly the plug-in it saw before.
    addAudioOutput(STR16("Stem A"), SpeakerArr::kStereo, kAux);
    addAudioOutput(STR16("Stem B"), SpeakerArr::kStereo, kAux);
    addAudioOutput(STR16("Stem C"), SpeakerArr::kStereo, kAux);
    addAudioOutput(STR16("Stem D"), SpeakerArr::kStereo, kAux);
    // auxiliary, not main: an instrument gets no track input, but a host can
    // still route audio to an aux bus for Record and Input Mix
    addAudioInput(STR16("Audio In"), SpeakerArr::kStereo, kAux);

    static const char* const kTriggerNames[kNumTriggers] = {"From Tape"};
    for (int i : mplat::knobOrder(kKnobPages, kFirstTrigger + kNumTriggers)) {
        if (i >= kFirstTrigger) {
            mplat::addTriggerParam(parameters, i, kTriggerNames[i - kFirstTrigger]);
            continue;
        }
        const ParamDef& p = kParams[i];
        char16 title[128];
        str8ToStr16(title, p.name, 128);

        if (p.names) {
            // a named list, so the host shows the value rather than a number
            auto* sp = new StringListParameter(title, i);
            for (int k = 0; k < p.numNames; k++) {
                char16 v[128];
                str8ToStr16(v, p.names[k], 128);
                sp->appendString(v);
            }
            parameters.addParameter(sp);
            // setNormalized sets the current value, not the one the host
            // resets to - without this Voicing defaults to Mono and Rate to
            // 1/2, neither of which is what the plug-in actually starts on.
            sp->getInfo().defaultNormalizedValue = norm(i, p.def);
            sp->setNormalized(norm(i, p.def));
        } else {
            // RangeParameter, not the plain one: the base Parameter's toPlain
            // is the identity, so it would report 0..1 for everything and a
            // 180 ms grain would read as 0.09.
            char16 units[64];
            str8ToStr16(units, p.unit, 64);
            const double dispDef = p.dispLo +
                curveOut(p, norm(i, p.def)) * (p.dispHi - p.dispLo);
            RangeParameter* rp = (p.curve != 1.0)
                ? new CurvedRangeParameter(title, i, p.unit[0] ? units : nullptr,
                                           p.dispLo, p.dispHi, dispDef,
                                           ParameterInfo::kCanAutomate, p)
                : new RangeParameter(title, i, p.unit[0] ? units : nullptr,
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

tresult PLUGIN_API TapeGrainVST::terminate() {
    triggers_.stop();
    editor_ = nullptr;
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API TapeGrainVST::setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                                    SpeakerArrangement* out, int32 numOut) {
    // The main out, optionally followed by the four stem outs, all stereo;
    // and either no aux input or a stereo one.
    if (numOut < 1 || numOut > 1 + tg::kStemSlots) return kResultFalse;
    for (int32 i = 0; i < numOut; i++)
        if (out[i] != SpeakerArr::kStereo) return kResultFalse;
    if (numIn > 1) return kResultFalse;
    if (numIn == 1 && in[0] != SpeakerArr::kStereo) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
}

tresult PLUGIN_API TapeGrainVST::activateBus(MediaType type, BusDirection dir,
                                             int32 index, TBool state) {
    const tresult r = SingleComponentEffect::activateBus(type, dir, index, state);
    if (type == kAudio && dir == kOutput) {
        bool any = false;
        for (int i = 1; i <= tg::kStemSlots; i++)
            if (auto* b = FCast<AudioBus>(audioOutputs.at(i))) any = any || b->isActive();
        dsp_.setSplitOutputs(any);
    }
    return r;
}

tresult PLUGIN_API TapeGrainVST::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API TapeGrainVST::setupProcessing(ProcessSetup& setup) {
    dsp_.setSampleRate(setup.sampleRate);
    // setSampleRate resets the engine, so push the parameters back in
    for (int i = 0; i < kNumDsp; i++) dsp_.setParam(i, cache_[i]);
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API TapeGrainVST::setActive(TBool state) {
    if (!state) dsp_.allNotesOff();      // never leave a voice hanging
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API TapeGrainVST::process(ProcessData& data) {
    // parameter changes first, so the block renders with current values
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

    if (data.processContext &&
        (data.processContext->state & ProcessContext::kTempoValid))
        dsp_.setHostTempo(data.processContext->tempo);

    if (data.numSamples <= 0) return kResultOk;
    if (data.numOutputs < 1 || data.outputs[0].numChannels < 2) return kResultOk;

    const int n = data.numSamples;
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];

    const float* inL = nullptr;
    const float* inR = nullptr;
    if (data.numInputs > 0 && data.inputs[0].numChannels >= 1) {
        inL = data.inputs[0].channelBuffers32[0];
        inR = (data.inputs[0].numChannels > 1)
            ? data.inputs[0].channelBuffers32[1] : nullptr;
    }

    if (dsp_.isRecording()) dsp_.captureInput(inL, inR, n);

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
                if (e.noteOn.velocity > 0.f)
                    dsp_.noteOn(e.noteOn.pitch, e.noteOn.velocity);
                else
                    dsp_.noteOff(e.noteOn.pitch);   // note-on at zero is a note-off
            } else if (e.type == Event::kNoteOffEvent) {
                dsp_.noteOff(e.noteOff.pitch);
            }
            ei++;
        }

        const int len = next - pos;
        if (len <= 0) break;                        // cannot happen; guard anyway
        dsp_.process(inL ? inL + pos : nullptr,
                     inR ? inR + pos : nullptr,
                     outL + pos, outR + pos, len);

        // Copied per segment, because the block is rendered in pieces split at
        // each note event and the engine's stem buffers only hold the piece it
        // has just done.
        if (dsp_.splitOutputs())
            for (int s = 0; s < tg::kStemSlots; s++) {
                if (data.numOutputs <= s + 1) break;
                if (data.outputs[s + 1].numChannels < 2) continue;
                float* aL = data.outputs[s + 1].channelBuffers32[0];
                float* aR = data.outputs[s + 1].channelBuffers32[1];
                if (!aL || !aR) continue;
                const float* sL = dsp_.stemOutL(s);
                const float* sR = dsp_.stemOutR(s);
                for (int i = 0; i < len; i++) {
                    aL[pos + i] = sL ? sL[i] : 0.f;
                    aR[pos + i] = sR ? sR[i] : 0.f;
                }
            }
        pos = next;
    }

    // An output bus the host gave us but we never wrote has to be cleared,
    // or it plays whatever was in that memory last block.
    if (!dsp_.splitOutputs())
        for (int32 b = 1; b < data.numOutputs; b++)
            for (int32 ch = 0; ch < data.outputs[b].numChannels; ch++)
                if (float* p = data.outputs[b].channelBuffers32[ch])
                    for (int i = 0; i < n; i++) p[i] = 0.f;

    // silent output still has to be reported, or hosts may keep the tail
    for (int32 b = 0; b < data.numOutputs; b++) data.outputs[b].silenceFlags = 0;
    return kResultOk;
}

// State carries a version. 2 held one sample path; 3 holds one per stem slot.
static const int32 kStateVersion = 3;

tresult PLUGIN_API TapeGrainVST::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);

    int32 version = 0, count = 0;
    if (!s.readInt32(version)) return kResultFalse;
    if (version != 2 && version != kStateVersion) return kResultFalse;
    if (!s.readInt32(count)) return kResultFalse;

    for (int i = 0; i < count; i++) {
        float v = 0;
        if (!s.readFloat(v)) return kResultFalse;
        if (i >= kNumDsp) continue;                 // a newer state, read past it
        cache_[i] = v;
        dsp_.setParam(i, v);
        SingleComponentEffect::setParamNormalized(i, norm(i, v));
    }

    // The sample paths, so a saved Set reloads its audio. Version 2 wrote a
    // single path, which is slot A here.
    // readStr8 allocates; the caller owns the result.
    const int paths = (version == 2) ? 1 : tg::kStemSlots;
    for (int slot = 0; slot < paths; slot++) {
        char8* path = s.readStr8();
        if (!path) break;
        if (*path) loadSample(slot, path);
        delete[] path;
    }
    dsp_.refreshSlices();
    return kResultOk;
}

tresult PLUGIN_API TapeGrainVST::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(kStateVersion);
    s.writeInt32(kNumDsp);
    for (int i = 0; i < kNumDsp; i++) s.writeFloat(cache_[i]);
    for (int slot = 0; slot < tg::kStemSlots; slot++)
        s.writeStr8(samplePath_[slot].c_str());
    return kResultOk;
}

IPlugView* PLUGIN_API TapeGrainVST::createView(FIDString name) {
    if (name && strcmp(name, ViewType::kEditor) == 0)
        return new TapeGrainEditor(this, this);
    return nullptr;
}

tresult PLUGIN_API TapeGrainVST::setParamNormalized(ParamID id, ParamValue v) {
    tresult r = SingleComponentEffect::setParamNormalized(id, v);
    if (r != kResultOk) return r;

    // This is the controller thread, so it is where a preset recall can
    // safely tell the host about every value it moved.
    if ((int)id == kPresetId) {
        int idx = (int)(denorm(kPresetId, v) + 0.5);
        if (idx != preset_) recallPreset(idx, true);
    }
    if (editor_) editor_->syncControl((int)id, v);
    return kResultOk;
}

//------------------------------------------------------------------------
bool TapeGrainVST::loadSample(const char* path) {
    return loadSample(dsp_.activeSlot(), path);
}

bool TapeGrainVST::loadSample(int slot, const char* path) {
    if (!path || !*path || slot < 0 || slot >= tg::kStemSlots) return false;
    tg::WavData w = tg::loadWav(path);
    if (!w.ok) return false;
    double sr = processSetup.sampleRate > 0 ? processSetup.sampleRate : 44100.0;
    std::vector<float> s = tg::resample(w.mono, w.sampleRate, (int)sr);
    dsp_.setSample(slot, s.data(), (int)s.size());
    samplePath_[slot] = path;
    // reloading a saved Set arrives through setState, not through the button,
    // so the redraw has to happen here rather than at the call site
    if (editor_) editor_->refreshWaveform();
    return true;
}

void TapeGrainVST::setParamFromUI(int id, double normalized) {
    if (id < 0 || id >= kTotalParams) return;
    applyParam((ParamID)id, normalized);
    SingleComponentEffect::setParamNormalized((ParamID)id, normalized);
    performEdit((ParamID)id, normalized);          // tells the host to record it
    if (id == kPresetId) {
        int idx = (int)(denorm(kPresetId, normalized) + 0.5);
        recallPreset(idx, true);
    }
}

// Recalling a preset writes its values into the live parameters and then gets
// out of the way - every knob stays free afterwards. It deliberately leaves
// the sample, position, pitch, level, input mix, freeze, record and tempo
// alone, so a recall never interrupts what is playing.
void TapeGrainVST::recallPreset(int index, bool notifyHost) {
    preset_ = index;
    if (index <= 0 || index > tg::kNumPresets) return;    // 0 is "--"

    float vals[tg::kNumParams];
    dsp_.applyPreset(index - 1, vals);
    for (int i = 0; i < kNumDsp; i++) {
        if (cache_[i] == vals[i]) continue;
        cache_[i] = vals[i];
        double nv = norm(i, vals[i]);
        SingleComponentEffect::setParamNormalized(i, nv);
        if (notifyHost) {
            beginEdit(i);
            performEdit(i, nv);
            endEdit(i);
        }
        if (editor_) editor_->syncControl(i, nv);
    }
}

int TapeGrainVST::loadStemSet(const char* path) {
    if (!path || !*path) return 0;
    std::string parts[tg::kStemSlots];
    if (tg::findStemSet(path, parts) == 0) return 0;

    int loaded = 0;
    for (int slot = 0; slot < tg::kStemSlots; slot++) {
        if (parts[slot].empty()) continue;
        if (loadSample(slot, parts[slot].c_str())) loaded++;
    }
    if (loaded > 0) {
        // Bring up every part that loaded and mute the rest: a separated set
        // starts as the track you separated, which is the thing you can then
        // pull apart. Anything else means opening to silence.
        for (int slot = 0; slot < tg::kStemSlots; slot++)
            setParamFromUI(tg::kStemA + slot,
                           parts[slot].empty() ? 0.0 : 1.0);

        // focus the first one that got something, so the waveform shows audio
        for (int slot = 0; slot < tg::kStemSlots; slot++) {
            if (!dsp_.slotFilled(slot)) continue;
            setParamFromUI(tg::kStem, norm(tg::kStem, (double)slot));
            break;
        }
    }
    return loaded;
}

//------------------------------------------------------------------------
// Presets on disk.
//
// Deliberately a plain text file keyed by parameter name. An index-keyed
// binary blob would silently mis-assign every value the next time a
// parameter is inserted, and this plug-in has grown its parameter list
// twice already.

std::string TapeGrainVST::presetFolder() {
    return mplat::userDataFolder("TapeGrain", "Presets");
}

bool TapeGrainVST::savePreset(const char* path) {
    const char* names[kNumDsp];
    for (int i = 0; i < kNumDsp; i++) names[i] = kParams[i].name;
    return tg::savePreset(path, names, cache_, kNumDsp,
                          samplePath_, tg::kStemSlots);
}

bool TapeGrainVST::loadPreset(const char* path) {
    return tg::loadPreset(path,
        [this](const std::string& name, double value) {
            for (int i = 0; i < kNumDsp; i++) {
                if (name != kParams[i].name) continue;
                double nv = std::max(0.0, std::min(1.0, norm(i, value)));
                cache_[i] = (float)denorm(i, nv);
                dsp_.setParam(i, cache_[i]);
                SingleComponentEffect::setParamNormalized(i, nv);
                // tell the host, so the change lands in its automation too
                beginEdit(i);
                performEdit(i, nv);
                endEdit(i);
                if (editor_) editor_->syncControl(i, nv);
                return;
            }
            // an unrecognised name is not an error: it is a set written by a
            // build that had a parameter this one does not
        },
        [this](int slot, const std::string& sample) {
            // A set whose sample has moved still loads - it just keeps
            // whatever is already in that slot.
            if (slot < 0 || slot >= tg::kStemSlots) return;
            if (sample != samplePath_[slot]) loadSample(slot, sample.c_str());
            dsp_.refreshSlices();
            if (editor_) editor_->refreshWaveform();
        });
}

void TapeGrainVST::buildOutline(int width, std::vector<float>& peaks,
                                std::string& caption) {
    peaks.clear();
    caption.clear();
    if (width <= 0) return;

    const std::vector<float>& b = dsp_.sampleData();
    if (b.empty()) {
        caption = dsp_.isRecording() ? "recording..." : "";
        return;
    }

    peaks.resize((size_t)width, 0.f);
    const double per = (double)b.size() / (double)width;
    for (int x = 0; x < width; x++) {
        size_t a = (size_t)(x * per);
        size_t z = (size_t)((x + 1) * per);
        if (z > b.size()) z = b.size();
        float m = 0.f;
        for (size_t i = a; i < z; i++) m = std::max(m, std::fabs(b[i]));
        peaks[(size_t)x] = std::min(1.f, m);
    }

    // filename only, plus which slot it is in, its length, and how many
    // onsets the analysis found
    const int slot = dsp_.activeSlot();
    std::string name = samplePath_[slot];
    size_t cut = name.find_last_of("/\\");
    if (cut != std::string::npos) name = name.substr(cut + 1);
    if (name.empty()) name = "recorded take";

    // Elide the middle rather than let the label clip an end. Live's stem
    // files carry their timestamp in the middle and the part that matters -
    // (Vocals), (Drums) - right at the end, so losing either end is worse
    // than losing what is between them.
    const size_t kRoom = 40;
    if (name.size() > kRoom) {
        const size_t head = kRoom / 2 - 2, tail = kRoom - head - 3;
        name = name.substr(0, head) + "..." + name.substr(name.size() - tail);
    }

    char buf[256];
    snprintf(buf, sizeof buf, "%c   %s   %.2f s   %d slices",
             'A' + slot, name.c_str(),
             (double)b.size() / dsp_.sampleRate(), dsp_.sliceCount());
    caption = buf;
}

void TapeGrainVST::applyParam(ParamID id, ParamValue norm01) {
    if ((int)id < kNumDsp) {
        float v = (float)denorm((int)id, norm01);
        cache_[id] = v;
        dsp_.setParam((int)id, v);
    } else if ((int)id == kRecordId) {
        dsp_.setRecording(norm01 >= 0.5);
    } else if ((int)id == kPresetId) {
        // Automation arrives here on the audio thread, where the host cannot
        // be told what moved; the values still land, they are just not
        // written back as automation.
        int idx = (int)(denorm(kPresetId, norm01) + 0.5);
        if (idx != preset_) recallPreset(idx, false);
    }
}

// The newest tape TapeRepair sent over, into the active slot.
std::string TapeGrainVST::runTrigger(int t) {
    if (t != kTrigFromTape) return {};
    const std::string path = mhand::newest();
    if (path.empty())
        return "Nothing has been sent over yet - repair a tape in "
               "TapeRepair and press SEND TO TAPEGRAIN.";
    if (!loadSample(path.c_str())) return "Found the tape but could not read it.";
    return "Loaded " + mhand::nameOf(path) + ".";
}

} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
// InitModule/DeinitModule are not defined here: with VSTGUI support enabled
// the SDK supplies them itself, and they are what run VSTGUI's own
// initialisation. Defining them again is a duplicate-symbol link error.

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(kTapeGrainUID),
           Steinberg::PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Steinberg::Vst::kSimpleModeSupported,
           // an instrument: notes play it, and it is silent at rest
           Steinberg::Vst::PlugType::kInstrumentSampler,
           FULL_VERSION_STR,
           kVstVersionString,
           Steinberg::Vst::TapeGrainVST::createInstance)

END_FACTORY
