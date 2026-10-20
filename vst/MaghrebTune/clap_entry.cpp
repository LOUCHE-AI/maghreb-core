// Maghreb Tune, as a CLAP.
//
// Everything below this file is shared: shared/clap_vst3.cpp runs the same VST3
// component the other format runs, so the engine, the parameters and the editor
// are the ones already tested. This says only what the plug-in is.

#include "clap_vst3.h"
#include "tune_vst.h"

namespace {

const char* const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_PITCH_CORRECTION,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDesc = {
    CLAP_VERSION_INIT,
    "fm.wael.maghrebtune",
    "Maghreb Tune",
    "Wael",
    "", "", "",
    "0.1.0",
    "A voice on a degree of the tabʿ, including the ones twelve-tone cannot reach.",
    kFeatures,
};

// Eight knobs a page, for a controller's encoders (a Launchkey in Plugin
// mode).
const mclap::RemotePage kPages[] = {
    {"Tune", {0, 1, 2, 3, 4, 5, 6, 7}},               // Tab Tonic TonicFine Amount Retune Deadzone Mix Output
    {"Mode", {10, 9, 8, -1, -1, -1, -1, -1}},         // Live Sensitivity Bypass
};

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::TuneVST::createInstance,
    true,     // takes audio: it corrects a voice
    false,    // and no notes
    false,    // notes out
    kPages,
    (int)(sizeof kPages / sizeof kPages[0]),
};

} // namespace

MCLAP_ENTRY(kSpec)
