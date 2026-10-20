// Maghreb Tubu, as a CLAP.
//
// Everything below this file is shared: shared/clap_vst3.cpp runs the same VST3
// component the other format runs, so the engine, the parameters and the editor
// are the ones already tested. This says only what the plug-in is.

#include "clap_vst3.h"
#include "tubu_vst.h"

namespace {

const char* const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDesc = {
    CLAP_VERSION_INIT,
    "fm.wael.maghrebtubu",
    "Maghreb Tubu",
    "Wael",
    "", "", "",
    "0.1.0",
    "The eighteen tubu, played rather than corrected to.",
    kFeatures,
};

// Eight knobs a page, for a controller's encoders (a Launchkey in Plugin
// mode). Revert stays off them: a nudged knob should not undo a tuning.
const mclap::RemotePage kPages[] = {
    {"Tuning", {0, 1, 2, 13, 14, 3, 4, -1}},          // Tab Tonic Mapping Degree Cents Drone DroneLevel
    {"Sound",  {5, 6, 7, 8, 9, 10, 11, 12}},          // Brightness Damping Decay Bow Attack Release Level Output
};

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::TubuVST::createInstance,
    false,     // audio in
    true,     // notes in
    false,     // notes out
    kPages,
    (int)(sizeof kPages / sizeof kPages[0]),
};

} // namespace

MCLAP_ENTRY(kSpec)
