// TapeRepair, as a CLAP.
//
// Everything below this file is shared: shared/clap_vst3.cpp runs the same VST3
// component the other format runs, so the engine, the parameters and the editor
// are the ones already tested. This says only what the plug-in is.

#include "clap_vst3.h"
#include "taperepair_vst.h"

namespace {

const char* const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_RESTORATION,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDesc = {
    CLAP_VERSION_INIT,
    "fm.wael.taperepair",
    "TapeRepair",
    "Wael",
    "", "", "",
    "0.1.0",
    "Record a tape transfer, mend it offline, play it back.",
    kFeatures,
};

// Eight knobs a page, for a controller's encoders (a Launchkey in Plugin
// mode). Arm and Revert stay off them: a nudged knob should not start a new
// take over the old one or throw a repair away.
const mclap::RemotePage kPages[] = {
    {"Repair", {0, 1, 2, 3, 4, 5, 6, 7}},             // Hum Harmonics Denoise Floor DCBlock Normalise Compare Output
    {"Tape",   {10, 11, 12, 8, 13, 14, 16, 17}},      // Play Follow AutoStop Bypass Learn Repair SplitSide Send
};

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::TapeRepairVST::createInstance,
    true,     // audio in
    false,     // notes in
    false,     // notes out
    kPages,
    (int)(sizeof kPages / sizeof kPages[0]),
};

} // namespace

MCLAP_ENTRY(kSpec)
