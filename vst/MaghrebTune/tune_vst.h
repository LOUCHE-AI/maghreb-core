// Maghreb Tune - VST3 wrapper.
//
// An audio effect, not an instrument: a voice goes in and the same voice comes
// out on a degree of the ṭabʿ. It reports its latency, because PSOLA needs a
// grain of lookahead and a host that does not know cannot line the vocal back
// up with the take.

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "tune_dsp.h"
#include "tune_params.h"
#include "tune_presets.h"

#include <string>
#include <vector>

namespace Steinberg {
namespace Vst {

class TuneEditor;

// The parameter table is shared with the CLAP wrapper: see tune_params.h.
using ParamDef = mtune::ParamDef;
inline const std::vector<ParamDef>& params() { return mtune::params(); }

static const int kNumDsp = mtune::kNumParams;
static const int kTotalParams = kNumDsp;

class TuneVST : public SingleComponentEffect {
public:
    TuneVST() {}
    static FUnknown* createInstance(void*);

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                          SpeakerArrangement* out,
                                          int32 numOut) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 s) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;
    uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;

    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE;
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) SMTG_OVERRIDE;

    void setParamFromUI(int id, double normalized);
    void setEditor(TuneEditor* e) { editor_ = e; }

    const mtune::EngineView& snapshot() const { return dsp_.snapshot(); }
    const char* tabName() const { return dsp_.tabName(); }
    int  neutralCount() const { return dsp_.neutralCount(); }
    int  degreeCents(int d) const { return dsp_.degreeCents(d); }
    void setDegreeFromUI(int degree, int cents);
    // Applied through setParamFromUI rather than straight into the DSP, so the
    // host sees every value the preset moved and can undo or automate them.
    void applyPreset(int index);
    int  currentPreset() const { return preset_; }
    void revertTuning();
    bool tuningEdited() const { return edited_; }

private:
    void applyParam(ParamID id, ParamValue norm01);

    mtune::MaghrebTune dsp_;
    float cache_[kNumDsp] = {0};
    TuneEditor* editor_ = nullptr;
    bool edited_ = false;
    int preset_ = 0;
};

} // namespace Vst
} // namespace Steinberg
