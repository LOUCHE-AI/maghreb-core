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

// Eight knobs a page, for a controller's encoders (a Launchkey in Plugin
// mode). A page per lane, the four levels and mutes together as a mixer, and
// the patch steps and exports last. The song chain is left to the editor.
const mclap::RemotePage kPages[] = {
    {"Main",   {0, 1, 2, 3, 4, 77, 78, 79}},          // Swing Master Output Song SongLoop Engine Tone Vel>FM
    {"Mixer",  {7, 15, 23, 31, 12, 20, 28, 36}},      // L1-L4 Level, L1-L4 Mute
    {"Lane 1", {5, 6, 7, 8, 9, 10, 11, 12}},          // Rhythm Kit Level Pan Tune Decay Drive Mute
    {"Lane 2", {13, 14, 15, 16, 17, 18, 19, 20}},
    {"Lane 3", {21, 22, 23, 24, 25, 26, 27, 28}},
    {"Lane 4", {29, 30, 31, 32, 33, 34, 35, 36}},
    {"Browse", {80, 81, 82, 83, 84, 85, 86, -1}},     // Prev/Next Patch, Prev/Next Fav, Write MIDI, Render Loop, Render Kit
};

const mclap::Vst3Spec kSpec = {
    &kDesc,
    Steinberg::Vst::SeqVST::createInstance,
    false,     // audio in
    true,     // notes in
    true,     // notes out
    kPages,
    (int)(sizeof kPages / sizeof kPages[0]),
};

} // namespace

MCLAP_ENTRY(kSpec)
