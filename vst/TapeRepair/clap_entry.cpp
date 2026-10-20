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

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::TapeRepairVST::createInstance,
    true,     // audio in
    false,     // notes in
    false,     // notes out
};

} // namespace

MCLAP_ENTRY(kSpec)
