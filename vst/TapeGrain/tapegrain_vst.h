// TapeGrain - VST3 wrapper, declaration.
//
// Split out of the .cpp so the editor can reach the plug-in: loading a file,
// arming Record, recalling a preset and drawing the buffer all need the
// object itself, not just parameter ids.

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "tapegrain_dsp.h"

#include <string>
#include <vector>

namespace Steinberg {
namespace Vst {

class TapeGrainEditor;

// What the engine wants and what a person should read are not the same
// number: the engine takes Position as 0..1, but nobody thinks in 0.25 - they
// think in 25%. lo/hi/def are the engine's units, dispLo/dispHi are what the
// host and the readouts show at those ends, and the normalized value in
// between is the same for both.
struct ParamDef {
    const char* name;
    double lo, hi, def;        // engine units
    int steps;                 // 0 = continuous; otherwise steps+1 values
    const char* const* names;  // enum value names, or null
    int numNames;
    const char* unit;          // "ms", "%", "dB", "st" ... "" for enums
    double dispLo, dispHi;     // displayed value at lo and at hi
    int precision;             // decimals shown
};

extern const ParamDef kParams[];
// One line per parameter id, shown in the editor when you hover a control.
// Several of these are only meaningful in combination with another setting,
// and saying so is the point of having them.
extern const char* const kParamHelp[];
static const int kNumDsp = tg::kNumParams;   // these map 1:1 onto the engine
static const int kRecordId = kNumDsp;        // extra: arm recording
static const int kPresetId = kNumDsp + 1;    // extra: recall a starting point
static const int kTotalParams = kNumDsp + 2;

class TapeGrainVST : public SingleComponentEffect {
public:
    TapeGrainVST() {}
    static FUnknown* createInstance(void*);

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                          SpeakerArrangement* out,
                                          int32 numOut) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 s) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE;
    // Tracks whether the host has asked for the per-stem outputs. Splitting
    // costs work in the grain loop, so it is only done when someone is
    // listening to the result.
    tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index,
                                   TBool state) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;

    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE;
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) SMTG_OVERRIDE;

    // ---- used by the editor ----
    bool loadSample(const char* path);          // into the active stem slot
    bool loadSample(int slot, const char* path);

    // Given any one file of a separated set, fills every slot it can find.
    // Returns how many loaded, or 0 if the file is not part of a stem set -
    // in which case the caller should load it as an ordinary sample.
    int loadStemSet(const char* path);

    // Your own sets, as files. Keyed by parameter name rather than index, so
    // a preset saved today still loads after the parameter list grows again -
    // unknown names are skipped, missing ones keep their current value.
    bool savePreset(const char* path);
    bool loadPreset(const char* path);
    static std::string presetFolder();     // created on demand
    const std::string& samplePath(int slot) const { return samplePath_[slot]; }
    int   activeSlot() const { return dsp_.activeSlot(); }
    bool  slotFilled(int slot) const { return dsp_.slotFilled(slot); }
    float stemLevel(int slot) const { return dsp_.stemLevel(slot); }
    void setParamFromUI(int id, double normalized);
    void setEditor(TapeGrainEditor* e) { editor_ = e; }

    double playhead() const { return dsp_.playhead(); }
    const tg::EngineView& snapshot() const { return dsp_.snapshot(); }
    bool isRecording() const { return dsp_.isRecording(); }
    int  activeVoices() const { return dsp_.activeVoices(); }
    bool hasSample() const { return dsp_.hasSample(); }
    bool splitOutputs() const { return dsp_.splitOutputs(); }
    int  sliceCount() const { return dsp_.sliceCount(); }
    const std::vector<double>& slices() const { return dsp_.slices(); }

    // Sensitivity re-slicing allocates, so it is deferred out of the audio
    // callback; the editor's idle tick is what actually runs it.
    void refreshSlices() { dsp_.refreshSlices(); }

    // Decimated |peak| per pixel plus a one-line description of what is
    // loaded. An empty outline is what the view draws its placeholder for.
    void buildOutline(int width, std::vector<float>& peaks, std::string& caption);

private:
    void applyParam(ParamID id, ParamValue norm01);
    // notifyHost is false on the audio thread: performEdit is not callable there
    void recallPreset(int index, bool notifyHost);

    tg::TapeGrain dsp_;
    float cache_[kNumDsp] = {0};
    std::string samplePath_[tg::kStemSlots];
    TapeGrainEditor* editor_ = nullptr;
    int preset_ = 0;
};

} // namespace Vst
} // namespace Steinberg
