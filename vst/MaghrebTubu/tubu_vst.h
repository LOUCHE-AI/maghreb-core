// Maghreb Tubu - VST3 wrapper, declaration.

#pragma once

#include "knob_pages.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "tubu_dsp.h"

#include <string>
#include <vector>

namespace Steinberg {
namespace Vst {

class TubuEditor;

// Engine units and display units are not the same number: the engine takes
// Bow as 0..1, a person reads 0..100%.
struct ParamDef {
    const char* name;
    double lo, hi, def;
    int steps;                 // 0 = continuous; otherwise steps+1 values
    const char* const* names;  // enum value names, or null
    int numNames;
    const char* unit;
    double dispLo, dispHi;
    int precision;
};

extern const ParamDef kParams[];
extern const char* const kParamHelp[];

static const int kNumDsp = tubu::kNumParams;
static const int kRevertId = kNumDsp;          // wrapper-only
static const int kTotalParams = kNumDsp + 1;

// The knobs, eight to a bank (shared/knob_pages.h). This order is the one
// Ableton gives a Launchkey or Launchpad, and Bitwig's Remote Controls pages.
// Anything that can throw work away comes last, a page-turn from the knobs.
static const mplat::KnobPage kKnobPages[] = {
    {"Tuning", {0, 1, 2, 13, 14, 3, 4, 11}},          // Tab Tonic Mapping Degree Cents Drone DroneLevel Level
    {"Sound",  {5, 6, 7, 8, 9, 10, 12, 15}},          // Brightness Damping Decay Bow Attack Release Output Revert
};

class TubuVST : public SingleComponentEffect {
public:
    TubuVST() {}
    static FUnknown* createInstance(void*);

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                          SpeakerArrangement* out,
                                          int32 numOut) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 s) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;

    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE;
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) SMTG_OVERRIDE;
    // Tonic reads better as a note name than as a MIDI number.
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue v,
                                             String128 str) SMTG_OVERRIDE;

    // ---- used by the editor ----
    void setParamFromUI(int id, double normalized);
    void setEditor(TubuEditor* e) { editor_ = e; }
    void revertTuning();
    // drag a degree on the ruler; phase 0 begins, 1 moves, 2 ends
    void setDegreeFromUI(int degree, int cents, int phase);

    const tubu::EngineView& snapshot() const { return dsp_.snapshot(); }
    const char* tabName() const { return dsp_.tabName(); }
    int  activeVoices() const { return dsp_.activeVoices(); }
    int  tonic() const { return (int)(dsp_.getParam(tubu::kTonic) + 0.5f); }
    static std::string noteName(int midi);

private:
    void applyParam(ParamID id, ParamValue norm01);

    tubu::Tubu dsp_;
    float cache_[kNumDsp] = {0};
    TubuEditor* editor_ = nullptr;
    int revert_ = 0;
};

} // namespace Vst
} // namespace Steinberg
