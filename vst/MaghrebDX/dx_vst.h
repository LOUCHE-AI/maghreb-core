// Maghreb DX - VST3 wrapper, declaration.

#pragma once

#include "knob_pages.h"
#include "patch_triggers.h"
#include "triggers.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "dx_dsp.h"

#include <string>
#include <vector>

namespace Steinberg {
namespace Vst {

class DXEditor;

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

static const int kNumDsp = mdx::kNumParams;
static const int kResetOneId = kNumDsp;       // wrapper-only
static const int kResetAllId = kNumDsp + 1;
static const int kTotalParams = kNumDsp + 2;
// Prev/Next Patch, Prev/Next Favourite: buttons for a controller, never saved.
static const int kFirstTrigger = kTotalParams;

// The knobs, eight to a bank (shared/knob_pages.h). This order is the one
// Ableton gives a Launchkey or Launchpad, and Bitwig's Remote Controls pages.
// Anything that can throw work away comes last, a page-turn from the knobs.
static const mplat::KnobPage kKnobPages[] = {
    {"Kit",            {0, 1, 2, 3, 4, 5, 6, 7}},         // Kit Tune Decay FM Noise Tone Drive Level
    {"Feel",           {8, 9, 10, 11, 12, 13, 14, 15}},   // Humanize Spread Vel>FM Vel>Tone Lowpass Highpass Output Stroke
    {"Stroke",         {16, 17, 18, 19, 20, 21, 27, 28}}, // Freq Ratio Index FMTime Length Bend Gain Pan
    {"Noise / Browse", {23, 24, 25, 26, 31, 32, 33, 34}}, // Noise NoiseFreq NoiseQ NoiseTime, Prev/Next Patch, Prev/Next Fav
    {"Bend / Reset",   {22, 29, 30, -1, -1, -1, -1, -1}}, // BendTime ResetStroke ResetKit
};

class DXVST : public SingleComponentEffect {
public:
    DXVST() {}
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
    void setEditor(DXEditor* e) { editor_ = e; }
    void selectStroke(int stroke);      // point the edit controls at it
    void audition(int stroke) { dsp_.queueTrigger(stroke); }
    void resetStroke(int stroke);
    void resetAll();

    // Kits on disk: every parameter plus all eleven strokes' own numbers.
    bool saveKit(const char* path);
    bool loadKit(const char* path);
    static std::string kitFolder();
    static std::string cartridgeFolder();

    // DX7 cartridges
    int  loadCartridge(const char* path) { return dsp_.loadCartridge(path); }
    int  cartridgeSize() const { return dsp_.cartridgeSize(); }
    const char* cartridgeName(int i) const { return dsp_.cartridgeName(i); }
    bool assignVoice(int stroke, int voice) { return dsp_.assignVoice(stroke, voice); }
    // Load a cartridge and put one of its voices on a pad in one step, which
    // is what a click in the browser means. The file is only re-read when it
    // is a different one.
    bool pickCartVoice(const char* path, int voice, int stroke);
    const std::string& cartPath() const { return cartPath_; }
    int cartVoice() const { return cartVoice_; }
    void clearVoice(int stroke) { dsp_.clearVoice(stroke); }
    bool usesImported(int s) const { return dsp_.usesImported(s); }
    const char* importedName(int s) const { return dsp_.importedName(s); }

    const mdx::EngineView& snapshot() const { return dsp_.snapshot(); }
    int  selected() const { return dsp_.selected(); }
    bool strokeEdited(int i) const { return dsp_.strokeEdited(i); }
    int  activeVoices() const { return dsp_.activeVoices(); }

    // Controller buttons. The editor drains these from its timer while open.
    mplat::Triggers& triggers() { return triggers_; }
    std::string runTrigger(int t, const mdx::CartIndex* idx = nullptr);

private:
    void applyParam(ParamID id, ParamValue norm01);
    void pushSelectedToUI();            // stroke table -> edit parameters

    mdx::MaghrebDX dsp_;
    float cache_[kNumDsp] = {0};
    DXEditor* editor_ = nullptr;
    std::string cartPath_;
    int cartVoice_ = 0;                 // the last voice picked from it
    mplat::Triggers triggers_;          // last: its thread stops before the rest goes
};

} // namespace Vst
} // namespace Steinberg
