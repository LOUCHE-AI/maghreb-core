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

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::TuneVST::createInstance,
    true,     // takes audio: it corrects a voice
    false,    // and no notes
    false,    // notes out
    Steinberg::Vst::kKnobPages,     // the knob banks, as Bitwig Remote Controls
    (int)(sizeof Steinberg::Vst::kKnobPages / sizeof Steinberg::Vst::kKnobPages[0]),
};

} // namespace

MCLAP_ENTRY(kSpec)
