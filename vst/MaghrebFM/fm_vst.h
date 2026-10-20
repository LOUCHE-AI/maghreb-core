// Maghreb FM - VST3 wrapper, declaration.

#pragma once

#include "knob_pages.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "fm_dsp.h"

#include <string>
#include <vector>

namespace Steinberg {
namespace Vst {

class FMEditor;

struct ParamDef {
    std::string name;
    double lo, hi, def;
    int steps;
    const char* const* names;
    int numNames;
    std::string unit;
    double dispLo, dispHi;
    int precision;
    std::string help;
};

const std::vector<ParamDef>& params();

static const int kNumDsp = mfm::kNumParams;
static const int kResetOneId = kNumDsp;       // wrapper-only
static const int kResetAllId = kNumDsp + 1;
static const int kTotalParams = kNumDsp + 2;

// The knobs, eight to a bank (shared/knob_pages.h). This order is the one
// Ableton gives a Launchkey or Launchpad, and Bitwig's Remote Controls pages.
// Anything that can throw work away comes last, a page-turn from the knobs.
static const mplat::KnobPage kKnobPages[] = {
    {"Kit",           {0, 1, 2, 3, 4, 5, 6, 7}},          // Kit Tune Decay FM Noise Tone Drive Level
    {"Feel",          {8, 9, 10, 11, 12, 13, 14, 15}},    // Humanize Spread Vel>FM Vel>Tone Lowpass Highpass Output Stroke
    {"Stroke",        {16, 17, 18, 19, 20, 21, 27, 28}},  // Freq Ratio Index FMTime Length Bend Gain Pan
    {"Noise / Reset", {23, 24, 25, 26, 22, 29, 30, -1}},  // Noise NoiseFreq NoiseQ NoiseTime BendTime ResetStroke ResetKit
};

class FMVST : public SingleComponentEffect {
public:
    FMVST() {}
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

    // ---- used by the editor ----
    void setParamFromUI(int id, double normalized);
    void setEditor(FMEditor* e) { editor_ = e; }
    void selectStroke(int stroke);      // point the edit controls at it
    void audition(int stroke) { dsp_.queueTrigger(stroke); }
    void resetStroke(int stroke);
    void resetAll();

    // Kits on disk: every parameter plus all eleven strokes' own numbers.
    bool saveKit(const char* path);
    bool loadKit(const char* path);
    static std::string kitFolder();

    const mfm::EngineView& snapshot() const { return dsp_.snapshot(); }
    int  selected() const { return dsp_.selected(); }
    bool strokeEdited(int i) const { return dsp_.strokeEdited(i); }
    int  activeVoices() const { return dsp_.activeVoices(); }

private:
    void applyParam(ParamID id, ParamValue norm01);
    void pushSelectedToUI();            // stroke table -> edit parameters

    mfm::MaghrebFM dsp_;
    float cache_[kNumDsp] = {0};
    FMEditor* editor_ = nullptr;
};

} // namespace Vst
} // namespace Steinberg
