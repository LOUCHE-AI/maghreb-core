// Tubu DX, as a CLAP.
//
// Everything below this file is shared: shared/clap_vst3.cpp runs the same VST3
// component the other format runs, so the engine, the parameters and the editor
// are the ones already tested. This says only what the plug-in is.

#include "clap_vst3.h"
#include "tubudx_vst.h"

namespace {

const char* const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDesc = {
    CLAP_VERSION_INIT,
    "fm.wael.tubudx",
    "Tubu DX",
    "Wael",
    "", "", "",
    "0.1.0",
    "A DX7 voice per pad, tuned to a tabʿ rather than to twelve.",
    kFeatures,
};

// Eight knobs a page, for a controller's encoders (a Launchkey in Plugin
// mode). Revert stays off them: a nudged knob should not undo a tuning.
const mclap::RemotePage kPages[] = {
    {"Tuning", {0, 1, 2, 3, 4, 5, 6, 7}},             // Tab Tonic Mapping Degree Cents Drone DroneLevel Voice
    {"Patch",  {7, 8, 9, 11, 12, 13, 14, -1}},        // Voice Level Output Prev/Next Patch Prev/Next Favourite
};

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::TubuDXVST::createInstance,
    false,     // audio in
    true,     // notes in
    false,     // notes out
    kPages,
    (int)(sizeof kPages / sizeof kPages[0]),
};

} // namespace

MCLAP_ENTRY(kSpec)
