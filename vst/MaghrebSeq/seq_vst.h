// Maghreb Seq - VST3 wrapper, declaration.
//
// An instrument: it takes the host's transport and plays the rhythms through
// its own drums. Live hosts no plug-in that emits notes, so a sequencer that
// drives something else cannot be a VST3 at all - which is why the drums are
// in here rather than at the end of a MIDI cable.
//
// Both engines are carried: the Move module's two-operator FM, and the DX7
// six-operator engine that Maghreb DX uses, with the strokes derived into
// patches. They share one editable stroke table, so a kit saved out of
// Maghreb FM or Maghreb DX is the kit this sequencer plays, on either.

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "seq_dsp.h"
#include "seq_preset.h"

#include <string>
#include <vector>

namespace Steinberg {
namespace Vst {

class SeqEditor;

struct ParamDef {
    std::string name;
    double lo, hi, def;
    int steps;
    const char* const* names;   // enum value names, or null
    int numNames;
    std::string unit;
    double dispLo, dispHi;
    int precision;
    std::string help;
};

// Built once, because 77 parameters across four lanes and eight sections are
// better generated than typed.
const std::vector<ParamDef>& params();

static const int kNumDsp = mseq::kNumParams;
static const int kTotalParams = kNumDsp;

class SeqVST : public SingleComponentEffect {
public:
    SeqVST() {}
    static FUnknown* createInstance(void*);

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                          SpeakerArrangement* out,
                                          int32 numOut) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 s) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
    // Sends the block's hits out of the event bus. Split out of process()
    // because it is a separate question from making the sound.
    void sendNotes(ProcessData& data);
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;

    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE;
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) SMTG_OVERRIDE;

    void setParamFromUI(int id, double normalized);
    void setEditor(SeqEditor* e) { editor_ = e; }

    const mseq::EngineView& snapshot() const { return dsp_.snapshot(); }
    const char* rhythmName(int lane) const {
        return mseq::kRhythmNames[dsp_.rhythmOf(lane)];
    }
    const char* rhythmMeter(int lane) const {
        return mseq::kRhythmMeters[dsp_.rhythmOf(lane)];
    }
    const char* rhythmSteps(int lane) const {
        return mseq::kRhythms[dsp_.rhythmOf(lane)];
    }
    int activeVoices() const { return dsp_.activeVoices(); }

    bool loadKit(const char* path) { return dsp_.loadKit(path); }

    // A DX7 voice on a lane. The cartridge is remembered by path and voice
    // number rather than by its bytes, so a Set follows the file.
    bool pickLaneVoice(const char* path, int voice, int lane);
    void clearLaneVoice(int lane);
    bool laneHasVoice(int lane) const { return dsp_.laneHasVoice(lane); }
    const char* laneVoiceName(int lane) const { return dsp_.laneVoiceName(lane); }
    const std::string& laneCartPath(int lane) const {
        static const std::string none;
        return (lane >= 0 && lane < mseq::kLanes) ? laneCart_[lane] : none;
    }
    int laneCartVoice(int lane) const {
        return (lane >= 0 && lane < mseq::kLanes) ? laneVoiceIdx_[lane] : 0;
    }
    static std::string cartridgeFolder();

    // ---- what the pattern can become ----
    //
    // The rhythms are the reason this plug-in exists, and until now the only
    // way out of it was its own audio output. These write the pattern down
    // instead: as MIDI to play anything with, as a loop, or as a kit of one
    // shots. All three read the same walk of the pattern the sequencer plays.
    static std::string exportFolder();
    double sampleRate() const { return sampleRate_; }
    std::string exportMidi(std::string* err = nullptr);
    std::string exportAudio(std::string* err = nullptr);
    int exportKit(std::string* err = nullptr);
    void clearKit() { dsp_.resetKit(); }
    const std::string& kitPath() const { return dsp_.kitPath(); }
    static std::string kitFolder();
    static std::string kitLabel(const std::string& path);

    // ---- the patch page ----
    void patchFor(int stroke, uint8_t out[156]) const { dsp_.patchFor(stroke, out); }
    bool strokeEdited(int stroke) const { return dsp_.strokeEdited(stroke); }
    void setPatchByte(int stroke, int off, int v, int lo, int hi) {
        dsp_.setPatchByte(stroke, off, v, lo, hi);
        dirty_ = true;
    }
    void revertStroke(int stroke) { dsp_.revertStroke(stroke); dirty_ = true; }
    bool operatorWave(int stroke, int op, float* out, int n) const {
        return dsp_.operatorWave(stroke, op, out, n);
    }
    void auditionStroke(int stroke);

    // ---- sets ----
    //
    // A set is the whole page: every parameter, plus which kit and which
    // cartridge voice each lane is on. Saved by name so the file survives the
    // parameter list growing, which it has done twice already.
    bool saveSet(const char* path);
    bool loadSet(const char* path);
    void initSet();
    static std::string setFolder();
    // Every .mseq in the folder, sorted, so the editor can step through them
    // without opening a dialog.
    static std::vector<std::string> listSets();
    const std::string& setPath() const { return setPath_; }
    std::string setName() const { return kitLabel(setPath_); }
    // true once anything has been touched since the last save or load
    bool setDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }

private:
    void applyParam(ParamID id, ParamValue norm01);

    mseq::MaghrebSeq dsp_;

    double sampleRate_ = 48000.0;
    float cache_[kNumDsp] = {0};
    SeqEditor* editor_ = nullptr;
    std::string laneCart_[mseq::kLanes];
    int laneVoiceIdx_[mseq::kLanes] = {0};
    std::string setPath_;
    bool dirty_ = false;
};

} // namespace Vst
} // namespace Steinberg
