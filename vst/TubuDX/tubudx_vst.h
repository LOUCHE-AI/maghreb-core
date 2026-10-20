// Tubu DX - VST3 wrapper, declaration.

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "tubudx_dsp.h"

#include <string>
#include <vector>

namespace Steinberg {
namespace Vst {

class TubuDXEditor;

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

static const int kNumDsp = tdx::kNumParams;
static const int kRevertId = kNumDsp;          // wrapper-only
static const int kTotalParams = kNumDsp + 1;

class TubuDXVST : public SingleComponentEffect {
public:
    TubuDXVST() {}
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
    void setEditor(TubuDXEditor* e) { editor_ = e; }
    void revertTuning();

    // DX7 cartridges, shared with Maghreb DX
    int  loadCartridge(const char* path) { return dsp_.loadCartridge(path); }
    // Load a cartridge and select a voice in one step, which is what the
    // browser asks for. Reloads the file only when it is a different one.
    bool pickCartVoice(const char* path, int voice);
    const std::string& cartPath() const { return cartPath_; }
    int  cartridgeSize() const { return dsp_.cartridgeSize(); }
    const char* voiceName(int i) const { return dsp_.voiceName(i); }
    static std::string cartridgeFolder();
    void currentPatch(uint8_t out[156]) const { dsp_.currentPatch(out); }
    bool operatorWave(int op, float* out, int n) const {
        return dsp_.operatorWave(op, out, n);
    }
    bool patchEdited() const { return dsp_.patchEdited(); }
    int currentVoice() const { return dsp_.voiceIndex(); }
    void setPatchByte(int off, int v, int lo, int hi) { dsp_.setPatchByte(off, v, lo, hi); }
    void revertPatch() { dsp_.revertPatch(); }
    // drag a degree on the ruler; phase 0 begins, 1 moves, 2 ends
    void setDegreeFromUI(int degree, int cents, int phase);

    const tdx::EngineView& snapshot() const { return dsp_.snapshot(); }
    const char* tabName() const { return dsp_.tabName(); }
    int  activeVoices() const { return dsp_.activeVoices(); }
    int  tonic() const { return (int)(dsp_.getParam(tdx::kTonic) + 0.5f); }
    static std::string noteName(int midi);

private:
    void applyParam(ParamID id, ParamValue norm01);

    tdx::TubuDX dsp_;
    float cache_[kNumDsp] = {0};
    TubuDXEditor* editor_ = nullptr;
    int revert_ = 0;
    std::string cartPath_;
};

} // namespace Vst
} // namespace Steinberg
