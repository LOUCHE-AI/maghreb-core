// Maghreb Seq - VST3 wrapper.

#include "seq_vst.h"
#include "seq_editor.h"
#include "dx_sysex.h"
#include "trigger_params.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstevents.h"   // Event and IEventList, for the notes out   // ProcessContext, not just the pointer
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"
#include "platform.h"
#include "seq_export.h"

#include <cstring>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <cstdlib>
#include <cmath>
#include <map>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#define stringPluginName "Maghreb Seq"
#define stringCompanyName "Wael"
#define stringCompanyWeb "https://example.invalid"
#define stringCompanyEmail "mailto:noreply@example.invalid"
#define FULL_VERSION_STR "0.1.0"

static const Steinberg::FUID kSeqUID(0x9F14B2C7, 0x3A5D48E6, 0x8C0719BD, 0x62E4A035);

namespace Steinberg {
namespace Vst {

static const char* const kOffOn[] = {"Off", "On"};
static const char* const kKitNames[] = {"Bendir", "Darbuka", "Tar"};
static const char* const kEngineNames[] = {"FM", "DX7"};

// The rhythm names are the module's, held as a stable array of pointers for
// StringListParameter.
static const char* const* rhythmNames() {
    static std::vector<const char*> v;
    if (v.empty())
        for (int i = 0; i < mseq::kNumRhythms; i++) v.push_back(mseq::kRhythmNames[i]);
    return v.data();
}

const std::vector<ParamDef>& params() {
    static std::vector<ParamDef> P;
    if (!P.empty()) return P;
    P.resize(mseq::kNumParams);

    auto set = [&](int id, const char* name, double lo, double hi, double def,
                   int steps, const char* const* names, int numNames,
                   const char* unit, double dLo, double dHi, int prec,
                   const char* help) {
        ParamDef& d = P[id];
        d.name = name; d.lo = lo; d.hi = hi; d.def = def; d.steps = steps;
        d.names = names; d.numNames = numNames; d.unit = unit;
        d.dispLo = dLo; d.dispHi = dHi; d.precision = prec; d.help = help;
    };

    set(mseq::kSwing, "Swing", 0, 1, 0, 0, nullptr, 0, "%", 0, 100, 0,
        "Holds the odd sixteenths back. The rhythms are written straight, so this is what gives them a lean.");
    set(mseq::kMaster, "Master", 0, 1, 0.8, 0, nullptr, 0, "%", 0, 100, 0,
        "Level of the whole kit, before Output.");
    set(mseq::kOutput, "Output", -60, 12, 0, 0, nullptr, 0, "dB", -60, 12, 1,
        "Master trim. Can boost as well as cut.");
    set(mseq::kSongOn, "Song", 0, 1, 0, 1, kOffOn, 2, "", 0, 1, 0,
        "Off loops the lanes forever. On follows the section chain below, so the arrangement changes as it plays.");
    set(mseq::kSongLoop, "Song Loop", 0, 1, 1, 1, kOffOn, 2, "", 0, 1, 0,
        "Whether the section chain repeats when it reaches the end, or holds on the last section.");
    set(mseq::kEngine, "Engine", 0, 1, 0, 1, kEngineNames, 2, "", 0, 1, 0,
        "Which synth the lanes strike. FM is the Move module's two operators. DX7 rebuilds each stroke as a six-operator voice - fuller, with the noise layer on its own operator pair. Hits already sounding ring out when you switch.");
    set(mseq::kTone, "Tone", 0, 1, 0.5, 0, nullptr, 0, "%", 0, 100, 0,
        "Tilts the noise band on every drum. Down is a dull skin, up is a bright rim.");
    set(mseq::kVelFM, "Vel>FM", 0, 1, 0.55, 0, nullptr, 0, "%", 0, 100, 0,
        "How much a softer hit takes the FM index down on the DX7 engine, so quiet strokes are dull rather than merely quiet. No effect on FM.");

    static const char* laneHelp[] = {
        "Which of the 90 rhythms this lane plays. Each keeps its own cycle length, so a 9/4 lane and a 4/4 lane drift in and out of phase the way they should.",
        "Which drum this lane strikes. The three kits scale the engine's frequency, decay, noise and FM index.",
        "Level of this lane.",
        "Where this lane sits in the stereo field.",
        "Tunes this lane's drum in semitones.",
        "Scales every envelope in the drum. Short is tight and dry, long rings.",
        "Saturation on the drum. The engine's own tanh stage, so it thickens rather than distorts.",
        "Silences this lane, whatever the song sections say.",
    };
    for (int l = 0; l < mseq::kLanes; l++) {
        char n[64];
        snprintf(n, sizeof n, "L%d Rhythm", l + 1);
        set(mseq::laneParam(l, mseq::kLaneRhythm), n, 0, mseq::kNumRhythms - 1,
            0, mseq::kNumRhythms - 1, rhythmNames(), mseq::kNumRhythms,
            "", 0, mseq::kNumRhythms - 1, 0, laneHelp[0]);
        snprintf(n, sizeof n, "L%d Kit", l + 1);
        set(mseq::laneParam(l, mseq::kLaneKit), n, 0, 2, 0, 2, kKitNames, 3,
            "", 0, 2, 0, laneHelp[1]);
        snprintf(n, sizeof n, "L%d Level", l + 1);
        set(mseq::laneParam(l, mseq::kLaneLevel), n, 0, 1, 0.9, 0, nullptr, 0,
            "%", 0, 100, 0, laneHelp[2]);
        snprintf(n, sizeof n, "L%d Pan", l + 1);
        set(mseq::laneParam(l, mseq::kLanePan), n, 0, 1, 0.5, 0, nullptr, 0,
            "%", 0, 100, 0, laneHelp[3]);
        snprintf(n, sizeof n, "L%d Tune", l + 1);
        set(mseq::laneParam(l, mseq::kLaneTune), n, -12, 12, 0, 48, nullptr, 0,
            "st", -12, 12, 1, laneHelp[4]);
        snprintf(n, sizeof n, "L%d Decay", l + 1);
        set(mseq::laneParam(l, mseq::kLaneDecay), n, 0.1, 3, 1, 0, nullptr, 0,
            "x", 0.1, 3, 2, laneHelp[5]);
        snprintf(n, sizeof n, "L%d Drive", l + 1);
        set(mseq::laneParam(l, mseq::kLaneDrive), n, 0, 1, 0.12, 0, nullptr, 0,
            "%", 0, 100, 0, laneHelp[6]);
        snprintf(n, sizeof n, "L%d Mute", l + 1);
        set(mseq::laneParam(l, mseq::kLaneMute), n, 0, 1, l == 0 ? 0 : 1, 1,
            kOffOn, 2, "", 0, 1, 0, laneHelp[7]);
    }

    for (int s = 0; s < mseq::kSections; s++) {
        char n[64];
        snprintf(n, sizeof n, "S%d Bars", s + 1);
        set(mseq::sectionParam(s, mseq::kSectionBars), n, 0, 16,
            s < 2 ? 4 : 0, 16, nullptr, 0, "", 0, 16, 0,
            "How many bars this section lasts. Zero skips it, so a short song is just a few sections.");
        for (int l = 0; l < mseq::kLanes; l++) {
            snprintf(n, sizeof n, "S%d L%d", s + 1, l + 1);
            set(mseq::sectionParam(s, mseq::kSectionL1 + l), n, 0, 1,
                (s == 0 && l == 0) ? 1 : (s == 1 ? 1 : 0), 1, kOffOn, 2,
                "", 0, 1, 0,
                "Whether this lane plays during this section. This is where an arrangement comes from.");
        }
    }
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

FUnknown* SeqVST::createInstance(void*) { return (IAudioProcessor*)new SeqVST(); }

tresult PLUGIN_API SeqVST::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addEventInput(STR16("MIDI In"), 16);
    addEventOutput(STR16("MIDI Out"), 16);
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
    static const char* const kTriggerNames[kNumTriggers] = {
        mdx::kPatchTriggerNames[0], mdx::kPatchTriggerNames[1],
        mdx::kPatchTriggerNames[2], mdx::kPatchTriggerNames[3],
        "Write MIDI", "Render Loop", "Render Kit",
    };
    mplat::addTriggerParams(parameters, kFirstTrigger, kTriggerNames, kNumTriggers);
    triggers_.start([this](int t) { runTrigger(t); });
    return kResultOk;
}

tresult PLUGIN_API SeqVST::terminate() {
    triggers_.stop();
    editor_ = nullptr;
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API SeqVST::setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                              SpeakerArrangement* out, int32 numOut) {
    if (numOut == 1 && out[0] == SpeakerArr::kStereo && numIn == 0)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

tresult PLUGIN_API SeqVST::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API SeqVST::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate > 0 ? setup.sampleRate : 48000.0;
    dsp_.setSampleRate(setup.sampleRate);
    for (int i = 0; i < kNumDsp; i++) dsp_.setParam(i, cache_[i]);
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API SeqVST::process(ProcessData& data) {
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

    // The whole sequencer follows this: position in quarter notes, and whether
    // the host is rolling. Nothing is counted, so several instances stay
    // locked to each other through loops and scrubbing.
    bool playing = false;
    double beats = 0;
    if (data.processContext) {
        const ProcessContext& c = *data.processContext;
        playing = (c.state & ProcessContext::kPlaying) != 0;
        if (c.state & ProcessContext::kProjectTimeMusicValid) beats = c.projectTimeMusic;
        if (c.state & ProcessContext::kTempoValid) dsp_.setTempo(c.tempo);
    }
    dsp_.setTransport(beats, playing);

    if (data.numSamples <= 0) return kResultOk;
    if (data.numOutputs < 1 || data.outputs[0].numChannels < 2) return kResultOk;

    dsp_.process(data.outputs[0].channelBuffers32[0],
                 data.outputs[0].channelBuffers32[1], data.numSamples);
    data.outputs[0].silenceFlags = 0;

    sendNotes(data);
    return kResultOk;
}

// The same hits, on their way out as notes.
//
// Read from what the engine just struck rather than worked out again here, so
// a muted lane or a song section that stops a drum stops the note with it. The
// pitches are the ones Maghreb DX and Tubu DX already answer to, so this plug-in
// drives them with nothing in between.
//
// Note offs go out in the same block as their ons. These are one shots - a
// struck drum is over long before any note length matters - and keeping the
// pair together means a host never holds a note open because a block boundary
// fell in the wrong place.
void SeqVST::sendNotes(ProcessData& data) {
    if (!data.outputEvents) return;
    const int n = data.numSamples;

    for (int i = 0; i < dsp_.firedCount(); i++) {
        const mseq::MaghrebSeq::Fired& f = dsp_.fired(i);
        const int32 at = (int32)std::max(0, std::min(n - 1, f.offset));

        Steinberg::Vst::Event on{};
        on.busIndex = 0;
        on.sampleOffset = at;
        on.flags = Steinberg::Vst::Event::kIsLive;
        on.type = Steinberg::Vst::Event::kNoteOnEvent;
        on.noteOn.channel = 9;                       // the drum channel
        on.noteOn.pitch = (int16)(mdx::kFirstNote + f.stroke);
        on.noteOn.velocity = 0.8f;
        on.noteOn.length = 0;
        on.noteOn.noteId = -1;
        on.noteOn.tuning = 0.f;
        data.outputEvents->addEvent(on);

        Steinberg::Vst::Event off{};
        off.busIndex = 0;
        off.sampleOffset = (int32)std::min(n - 1, at + 1);
        off.flags = Steinberg::Vst::Event::kIsLive;
        off.type = Steinberg::Vst::Event::kNoteOffEvent;
        off.noteOff.channel = 9;
        off.noteOff.pitch = on.noteOn.pitch;
        off.noteOff.velocity = 0.f;
        off.noteOff.noteId = -1;
        off.noteOff.tuning = 0.f;
        data.outputEvents->addEvent(off);
    }
}

// 2 adds the kit path, 3 the cartridge voice on each lane. Older Sets still
// load; they simply have neither, which is what they played.
static const int32 kStateVersion = 4;

tresult PLUGIN_API SeqVST::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer st(state, kLittleEndian);
    int32 version = 0, count = 0;
    if (!st.readInt32(version) || version < 1 || version > kStateVersion)
        return kResultFalse;
    if (!st.readInt32(count)) return kResultFalse;
    for (int i = 0; i < count; i++) {
        float v = 0;
        if (!st.readFloat(v)) return kResultFalse;
        if (i >= kNumDsp) continue;
        cache_[i] = v;
        dsp_.setParam(i, v);
        SingleComponentEffect::setParamNormalized(i, norm(i, v));
    }

    dsp_.resetKit();
    if (version >= 2) {
        int32 len = 0;
        if (st.readInt32(len) && len > 0 && len < 4096) {
            std::string path((size_t)len, '\0');
            if (st.readRaw(&path[0], len) == len) dsp_.loadKit(path.c_str());
        }
    }
    for (int i = 0; i < mseq::kLanes; i++) clearLaneVoice(i);
    if (version >= 3) {
        for (int i = 0; i < mseq::kLanes; i++) {
            int32 len = 0, voice = 0;
            if (!st.readInt32(len)) break;
            if (len <= 0 || len >= 4096) { st.readInt32(voice); continue; }
            std::string path((size_t)len, ' ');
            if (st.readRaw(&path[0], len) != len) break;
            if (!st.readInt32(voice)) break;
            pickLaneVoice(path.c_str(), voice, i);
        }
    }
    // The edited stroke patches. Bytes, not a file reference, because there is
    // no file for them to point at.
    dsp_.revertAllStrokes();
    if (version >= 4) {
        for (int i = 0; i < mseq::kNumStrokes; i++) {
            int32 ed = 0;
            if (!st.readInt32(ed)) break;
            if (!ed) continue;
            uint8_t bytes[156];
            if (st.readRaw(bytes, 156) != 156) break;
            dsp_.loadEdited(i, bytes);
        }
    }
    return kResultOk;
}

tresult PLUGIN_API SeqVST::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer st(state, kLittleEndian);
    st.writeInt32(kStateVersion);
    st.writeInt32(kNumDsp);
    for (int i = 0; i < kNumDsp; i++) st.writeFloat(cache_[i]);

    // The path, not the kit itself. A kit is a shared thing - the same file is
    // loaded by Maghreb FM and Maghreb DX - so a Set that embedded a copy
    // would quietly stop matching the kit the moment it was edited elsewhere.
    const std::string& kp = dsp_.kitPath();
    st.writeInt32((int32)kp.size());
    if (!kp.empty()) st.writeRaw(kp.data(), (int32)kp.size());

    for (int i = 0; i < mseq::kLanes; i++) {
        st.writeInt32((int32)laneCart_[i].size());
        if (!laneCart_[i].empty())
            st.writeRaw(laneCart_[i].data(), (int32)laneCart_[i].size());
        st.writeInt32((int32)laneVoiceIdx_[i]);
    }
    // 4 adds the edited stroke patches. They are bytes, not files, so unlike
    // the kit and the cartridges they have to travel inside the Set.
    for (int i = 0; i < mseq::kNumStrokes; i++) {
        const bool ed = dsp_.strokeEdited(i);
        st.writeInt32(ed ? 1 : 0);
        if (!ed) continue;
        const uint8_t* b = dsp_.editedBytes(i);
        st.writeRaw(b, 156);
    }
    return kResultOk;
}

bool SeqVST::pickLaneVoice(const char* path, int voice, int lane) {
    if (!path || !*path || lane < 0 || lane >= mseq::kLanes) return false;
    std::vector<mdx::SysexVoice> cart;
    if (mdx::loadSysex(path, cart) <= 0) return false;
    if (voice < 0 || voice >= (int)cart.size()) return false;
    dsp_.setLaneVoice(lane, cart[voice].data, cart[voice].name);
    laneCart_[lane] = path;
    laneVoiceIdx_[lane] = voice;
    return true;
}

// Whatever a controller pressed, and the line the editor shows for it. The
// editor's own export buttons come through here too, so the two never say
// different things.
std::string SeqVST::runTrigger(int t, const mdx::CartIndex* idx) {
    char msg[300];
    if (t < mdx::kNumPatchTriggers) {
        const int lane = editLane();
        mdx::PatchRef p {laneCart_[lane], laneVoiceIdx_[lane]};
        if (!mdx::patchForTrigger(t, p, cartridgeFolder(), mdx::favouritesFile("MaghrebSeq"), idx))
            return mdx::noPatchMessage(t);
        if (!pickLaneVoice(p.path.c_str(), p.voice, lane)) return "That cartridge would not read.";
        markDirty();
        snprintf(msg, sizeof msg, "Lane %d plays \"%s\".", lane + 1, laneVoiceName(lane));
        return msg;
    }
    std::string err;
    if (t == kTrigRenderKit) {
        const int n = exportKit(&err);
        if (n > 0)
            snprintf(msg, sizeof msg, "Rendered %d strokes into the Exports folder, named by note.", n);
        else
            snprintf(msg, sizeof msg, "%s", err.empty() ? "Nothing to render." : err.c_str());
        return msg;
    }
    if (t != kTrigWriteMidi && t != kTrigRenderLoop) return {};
    const std::string path = (t == kTrigWriteMidi) ? exportMidi(&err) : exportAudio(&err);
    if (!path.empty()) {
        const size_t cut = path.find_last_of("/\\");
        snprintf(msg, sizeof msg, "Wrote %s into the Exports folder.",
                 (cut == std::string::npos ? path : path.substr(cut + 1)).c_str());
    } else {
        snprintf(msg, sizeof msg, "%s", err.empty() ? "Nothing to write." : err.c_str());
    }
    return msg;
}

void SeqVST::clearLaneVoice(int lane) {
    if (lane < 0 || lane >= mseq::kLanes) return;
    dsp_.clearLaneVoice(lane);
    laneCart_[lane].clear();
    laneVoiceIdx_[lane] = 0;
}

// Shared with Maghreb DX and Tubu DX, so all three browse one folder.
std::string SeqVST::cartridgeFolder() {
    return mplat::userDataFolder("MaghrebDX", "Cartridges");
}

// Shared with Maghreb FM and Maghreb DX, so the file selector opens where the
// kits already are.
std::string SeqVST::kitFolder() {
    return mplat::userDataFolder("MaghrebFM", "Kits");
}

std::string SeqVST::kitLabel(const std::string& path) {
    if (path.empty()) return "FACTORY";
    size_t a = path.find_last_of("/\\");
    std::string n = (a == std::string::npos) ? path : path.substr(a + 1);
    size_t d = n.find_last_of('.');
    if (d != std::string::npos) n = n.substr(0, d);
    return n;
}

// ------------------------------------------------------------- exports
//
// The work is in seq_export.h, which needs no host to run and so can be
// tested. These say where to put it.

std::string SeqVST::exportFolder() {
    return mplat::userDataFolder("MaghrebSeq", "Exports");
}

std::string SeqVST::exportMidi(std::string* err) {
    return mseq::xport::writeMidi(dsp_, exportFolder(), err);
}

std::string SeqVST::exportAudio(std::string* err) {
    return mseq::xport::writeLoop(dsp_, exportFolder(), (int)sampleRate_, err);
}

int SeqVST::exportKit(std::string* err) {
    return mseq::xport::writeKit(dsp_, exportFolder(), (int)sampleRate_, err);
}

// ---------------------------------------------------------------- sets

std::string SeqVST::setFolder() {
    return mplat::userDataFolder("MaghrebSeq", "Sets");
}

std::vector<std::string> SeqVST::listSets() {
    std::vector<std::string> out;
    std::error_code ec;
    const std::string dir = setFolder();
    if (!std::filesystem::exists(dir, ec)) return out;
    for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".mseq") continue;
        out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool SeqVST::saveSet(const char* path) {
    const std::vector<ParamDef>& P = params();
    std::vector<const char*> names;
    names.reserve(kNumDsp);
    for (int i = 0; i < kNumDsp; i++) names.push_back(P[i].name.c_str());

    mseq::SetRefs refs;
    refs.kitPath = dsp_.kitPath();
    refs.lane.resize(mseq::kLanes);
    for (int l = 0; l < mseq::kLanes; l++) {
        if (!dsp_.laneHasVoice(l)) continue;
        refs.lane[l].cart = laneCart_[l];
        refs.lane[l].voice = laneVoiceIdx_[l];
    }
    if (!mseq::saveSet(path, names.data(), cache_, kNumDsp, refs)) return false;
    setPath_ = path;
    dirty_ = false;
    return true;
}

bool SeqVST::loadSet(const char* path) {
    // Build the name lookup once rather than scanning the list per line: at
    // 80 parameters and 80 lines that is 6400 string compares otherwise.
    const std::vector<ParamDef>& P = params();
    std::map<std::string, int> byName;
    for (int i = 0; i < kNumDsp; i++) byName[P[i].name] = i;

    std::vector<float> pending(kNumDsp);
    for (int i = 0; i < kNumDsp; i++) pending[i] = cache_[i];

    mseq::SetRefs refs;
    const bool ok = mseq::loadSet(path,
        [&](const std::string& name, double v) {
            auto it = byName.find(name);
            if (it == byName.end()) return;          // a name we retired
            const ParamDef& d = P[it->second];
            pending[it->second] = (float)std::max(d.lo, std::min(d.hi, v));
        }, refs);
    if (!ok) return false;

    for (int i = 0; i < kNumDsp; i++) {
        cache_[i] = pending[i];
        dsp_.setParam(i, pending[i]);
        SingleComponentEffect::setParamNormalized(i, norm(i, pending[i]));
        performEdit(i, norm(i, pending[i]));
    }

    dsp_.resetKit();
    if (!refs.kitPath.empty()) dsp_.loadKit(refs.kitPath.c_str());

    for (int l = 0; l < mseq::kLanes; l++) {
        clearLaneVoice(l);
        if (l < (int)refs.lane.size() && refs.lane[l].voice >= 0 &&
            !refs.lane[l].cart.empty())
            pickLaneVoice(refs.lane[l].cart.c_str(), refs.lane[l].voice, l);
    }

    setPath_ = path;
    dirty_ = false;
    return true;
}

void SeqVST::initSet() {
    const std::vector<ParamDef>& P = params();
    for (int i = 0; i < kNumDsp; i++) {
        cache_[i] = (float)P[i].def;
        dsp_.setParam(i, cache_[i]);
        SingleComponentEffect::setParamNormalized(i, norm(i, cache_[i]));
        performEdit(i, norm(i, cache_[i]));
    }
    dsp_.resetKit();
    for (int l = 0; l < mseq::kLanes; l++) clearLaneVoice(l);
    setPath_.clear();
    dirty_ = false;
}

// Strike a stroke so an edit can be heard without waiting for the transport
// to come round to it. Lane 0's level and pan are used, because a stroke does
// not belong to a lane and something has to place it.
void SeqVST::auditionStroke(int stroke) {
    dsp_.audition(stroke);
}

IPlugView* PLUGIN_API SeqVST::createView(FIDString name) {
    if (name && strcmp(name, ViewType::kEditor) == 0) return new SeqEditor(this, this);
    return nullptr;
}

tresult PLUGIN_API SeqVST::setParamNormalized(ParamID id, ParamValue v) {
    tresult r = SingleComponentEffect::setParamNormalized(id, v);
    if (r == kResultOk && editor_) editor_->syncControl((int)id, v);
    return r;
}

void SeqVST::setParamFromUI(int id, double normalized) {
    if (id < 0 || id >= kTotalParams) return;
    applyParam((ParamID)id, normalized);
    SingleComponentEffect::setParamNormalized((ParamID)id, normalized);
    performEdit((ParamID)id, normalized);
}

void SeqVST::applyParam(ParamID id, ParamValue norm01) {
    if ((int)id >= kNumDsp) return;
    dirty_ = true;
    const float v = (float)denorm((int)id, norm01);
    cache_[id] = v;
    dsp_.setParam((int)id, v);
}

} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(kSeqUID),
           Steinberg::PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Steinberg::Vst::kSimpleModeSupported,
           Steinberg::Vst::PlugType::kInstrumentDrum,
           FULL_VERSION_STR,
           kVstVersionString,
           Steinberg::Vst::SeqVST::createInstance)

END_FACTORY
