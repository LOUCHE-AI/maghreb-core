// TapeRepair - VST3 wrapper, declaration.
//
// Split out of the .cpp so the editor can reach the plug-in: the waveform, the
// transport and the four offline actions all need the object itself rather
// than parameter ids.

#pragma once

#include "knob_pages.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "taperepair_dsp.h"
#include "triggers.h"

#include <string>

namespace Steinberg {
namespace Vst {

class TapeRepairEditor;

// What the engine wants and what a person should read are not the same
// number. lo/hi/def are the engine's units; dispLo/dispHi are what the host
// and the readouts show at those ends.
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

static const int kNumDsp  = trep::kNumParams;   // these map 1:1 onto the engine
static const int kArmId   = kNumDsp;            // extra: arm recording
static const int kPlayId  = kNumDsp + 1;        // extra: play the take back
// Appended rather than folded in with the engine's own, so every id that was
// already automated in somebody's project still means what it meant. These are
// how the recorder behaves, not what the restore chain does.
static const int kFollowId   = kNumDsp + 2;     // arm with the host transport
static const int kAutoStopId = kNumDsp + 3;     // end the take at the run-out
static const int kTotalParams = kNumDsp + 4;

// The buttons that are not settings, for a controller to press. Never saved.
static const int kFirstTrigger = kTotalParams;
enum { kTrigLearn, kTrigRepair, kTrigRevert, kTrigSplit, kTrigSend, kNumTriggers };

// The knobs, eight to a bank (shared/knob_pages.h). This order is the one
// Ableton gives a Launchkey or Launchpad, and Bitwig's Remote Controls pages.
// Anything that can throw work away comes last, a page-turn from the knobs.
static const mplat::KnobPage kKnobPages[] = {
    {"Repair",       {0, 1, 2, 3, 4, 5, 6, 7}},       // Hum Harmonics Denoise Floor DCBlock Normalise Compare Output
    {"Tape",         {10, 11, 12, 8, 13, 14, 16, 17}},// Play Follow AutoStop Bypass Learn Repair SplitSide Send
    {"Arm / Revert", {9, 15, -1, -1, -1, -1, -1, -1}},
};

class TapeRepairVST : public SingleComponentEffect {
public:
    TapeRepairVST() {}
    static FUnknown* createInstance(void*);

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 numIn,
                                          SpeakerArrangement* out, int32 numOut) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 s) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;
    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE;

    // ---- for the editor -------------------------------------------------
    //
    // These are the slow ones. They run on the editor's thread, and the engine
    // passes audio through untouched while they do.
    // Reads a file off disk into the take. WAV directly, anything else
    // through ffmpeg; see shared/audio_import.h for why that split.
    bool loadFile(const std::string& path, std::string* err = nullptr);

    bool learn();
    bool repair();
    void revert();

    // Writes the repair - or the take, if there is no repair - into
    // TapeGrain's samples folder, which is the whole reason these two plug-ins
    // know about each other. Returns the file written, or empty on failure.
    std::string sendToTapeGrain(std::string* err = nullptr);

    // Writes each song on the side as its own file, next to the handoff, and
    // returns how many. Same folder and the same name prefix, so they arrive
    // in TapeGrain like anything else sent over.
    int splitToFiles(std::string* err = nullptr);

    void setParamFromUI(int id, double normalized);
    double normalizedOf(int id) const;

    trep::TapeRepair& engine() { return dsp_; }
    const trep::TapeRepair& engine() const { return dsp_; }

    void setEditor(TapeRepairEditor* e) { editor_ = e; }

    // Presses one of those buttons and returns the line to show for it. The
    // editor's own buttons come through here too.
    mplat::Triggers& triggers() { return triggers_; }
    std::string runTrigger(int t);

private:
    void applyParam(int id, double normalized);

    trep::TapeRepair dsp_;
    TapeRepairEditor* editor_ = nullptr;
    double sampleRate_ = 48000.0;
    mplat::Triggers triggers_;          // last: its thread stops before the rest goes
};

} // namespace Vst
} // namespace Steinberg
