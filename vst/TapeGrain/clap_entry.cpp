// TapeGrain, as a CLAP.
//
// Everything below this file is shared: shared/clap_vst3.cpp runs the same VST3
// component the other format runs, so the engine, the parameters and the editor
// are the ones already tested. This says only what the plug-in is.

#include "clap_vst3.h"
#include "tapegrain_vst.h"

namespace {

const char* const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDesc = {
    CLAP_VERSION_INIT,
    "fm.wael.tapegrain",
    "TapeGrain",
    "Wael",
    "", "", "",
    "0.1.0",
    "A cloud of grains off a sample, with a stem per slot.",
    kFeatures,
};

// Eight knobs a page, for a controller's encoders (a Launchkey in Plugin
// mode). Record and Preset stay off them: a nudged knob should not record
// over a sample or swap every setting for a preset's.
const mclap::RemotePage kPages[] = {
    {"Grain",  {0, 1, 2, 3, 4, 5, 6, 7}},             // Position GrainSize Density Pitch Level GrainShape Attack Release
    {"Spray",  {8, 9, 10, 11, 12, 20, 19, 21}},       // Spray Jitter Reverse PanSpray Focus PitchSpread Drift Freeze
    {"Motion", {13, 14, 15, 16, 17, 18, 30, 29}},     // Motion ScanRate Span StepRate Affinity Cohesion Agents Rate
    {"Stems",  {32, 33, 34, 35, 36, 37, 22, 31}},     // Stem Grains StemA-D InputMix Output
    {"Play",   {23, 24, 25, 26, 27, 28, 40, -1}},     // PadMode Sensitivity SpreadKeys Voicing Sync Tempo FromTape
};

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::TapeGrainVST::createInstance,
    true,     // audio in
    true,     // notes in
    false,     // notes out
    kPages,
    (int)(sizeof kPages / sizeof kPages[0]),
};

} // namespace

MCLAP_ENTRY(kSpec)
