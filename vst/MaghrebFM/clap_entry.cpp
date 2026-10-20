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

// Eight knobs a page, for a controller's encoders (a Launchkey in Plugin
// mode). Reset Stroke and Reset Kit stay off them: a knob nudged by accident
// should never be able to throw a kit away.
const mclap::RemotePage kPages[] = {
    {"Kit",          {0, 1, 2, 3, 4, 5, 6, 7}},         // Kit Tune Decay FM Noise Tone Drive Level
    {"Feel",         {8, 9, 10, 11, 12, 13, 14, 15}},   // Humanize Spread Vel>FM Vel>Tone Lowpass Highpass Output Stroke
    {"Stroke",       {15, 16, 17, 18, 19, 20, 21, 22}}, // Stroke Freq Ratio Index FMTime Length Bend BendTime
    {"Stroke Noise", {15, 23, 24, 25, 26, 27, 28, -1}}, // Stroke Noise NoiseFreq NoiseQ NoiseTime Gain Pan
};

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::FMVST::createInstance,
    false,     // audio in
    true,     // notes in
    false,     // notes out
    kPages,
    (int)(sizeof kPages / sizeof kPages[0]),
};

} // namespace

MCLAP_ENTRY(kSpec)
