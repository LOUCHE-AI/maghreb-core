// Maghreb FM, as a CLAP.
//
// Everything below this file is shared: shared/clap_vst3.cpp runs the same VST3
// component the other format runs, so the engine, the parameters and the editor
// are the ones already tested. This says only what the plug-in is.

#include "clap_vst3.h"
#include "fm_vst.h"

namespace {

const char* const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_DRUM_MACHINE,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDesc = {
    CLAP_VERSION_INIT,
    "fm.wael.maghrebfm",
    "Maghreb FM",
    "Wael",
    "", "", "",
    "0.1.0",
    "The same eleven strokes, on two operators rather than six.",
    kFeatures,
};

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::FMVST::createInstance,
    false,     // audio in
    true,     // notes in
    false,     // notes out
    Steinberg::Vst::kKnobPages,     // the knob banks, as Bitwig Remote Controls
    (int)(sizeof Steinberg::Vst::kKnobPages / sizeof Steinberg::Vst::kKnobPages[0]),
};

} // namespace

MCLAP_ENTRY(kSpec)
