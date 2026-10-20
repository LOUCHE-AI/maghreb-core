// Maghreb Seq, as a CLAP.
//
// Everything below this file is shared: shared/clap_vst3.cpp runs the same VST3
// component the other format runs, so the engine, the parameters and the editor
// are the ones already tested. This says only what the plug-in is.

#include "clap_vst3.h"
#include "seq_vst.h"

namespace {

const char* const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_DRUM_MACHINE,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDesc = {
    CLAP_VERSION_INIT,
    "fm.wael.maghrebseq",
    "Maghreb Seq",
    "Wael",
    "", "", "",
    "0.1.0",
    "Ninety rhythms on four lanes, with a song chain on top.",
    kFeatures,
};

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::SeqVST::createInstance,
    false,     // audio in
    true,     // notes in
    true,     // notes out
};

} // namespace

MCLAP_ENTRY(kSpec)
