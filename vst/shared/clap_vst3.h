// Running one of this family's VST3 components inside a CLAP module.
//
// Bitwig made CLAP and treats it as its own format, so a modulator dropped on a
// parameter addresses it directly. What it does not do is make the editors
// portable: every one of these derives from the SDK's VSTGUIEditor and talks to
// its VST3 wrapper - seventy-nine distinct calls across the eight. Rewriting
// that to sit behind an interface is the expensive way to reach CLAP.
//
// The cheap way is this: the CLAP plug-in holds the VST3 component and passes
// everything through. Parameters come from its IEditController, audio from its
// IAudioProcessor, the window from its IPlugView. The editors do not change at
// all, and neither does the DSP.
//
// The one thing gained rather than merely preserved: on Linux, VSTGUI needs a
// run loop, and here it is built from the CLAP host's own timer and file
// descriptor extensions, per plug-in instance. The VST3 side takes that from a
// process-global the SDK installs once from whichever editor opened first,
// which is what has been taking the whole plug-in host down with it.

#pragma once

#include <clap/clap.h>
#include "pluginterfaces/base/funknown.h"

#include <cstring>

namespace mclap {

// Eight knobs' worth of parameters, the way a controller with eight encoders
// shows them: Bitwig puts these on a device's Remote Controls, which is what a
// Launchkey's encoders turn in Plugin mode. -1 leaves a knob empty.
struct RemotePage {
    const char* name;
    int ids[8];
};

struct Vst3Spec {
    const clap_plugin_descriptor_t* desc = nullptr;
    Steinberg::FUnknown* (*createInstance)(void*) = nullptr;
    bool audioIn = false;      // an effect, or a sampler that records
    bool noteIn = true;
    bool noteOut = false;      // the sequencer, which plays other things
    const RemotePage* pages = nullptr;
    int numPages = 0;
};

const clap_plugin_t* make(const Vst3Spec& spec, const clap_host_t* host);

// The SDK's module entry, which a VST3 host calls on load and a CLAP host does
// not know about. It is what runs the registered init functions - VSTGUI's
// among them, and without it the first editor to open dereferences a platform
// factory that was never built.
bool moduleInit(const char* modulePath);
void moduleDeinit();

} // namespace mclap

// One plug-in per module, which is what every one of these is.
#define MCLAP_ENTRY(SPEC)                                                       \
    namespace {                                                                 \
    uint32_t CLAP_ABI mclapCount(const clap_plugin_factory_t*) { return 1; }    \
    const clap_plugin_descriptor_t* CLAP_ABI mclapDesc(                         \
        const clap_plugin_factory_t*, uint32_t i) {                             \
        return i == 0 ? (SPEC).desc : nullptr;                                  \
    }                                                                           \
    const clap_plugin_t* CLAP_ABI mclapCreate(const clap_plugin_factory_t*,     \
                                              const clap_host_t* host,          \
                                              const char* id) {                 \
        if (!id || std::strcmp(id, (SPEC).desc->id) != 0) return nullptr;       \
        return mclap::make((SPEC), host);                                       \
    }                                                                           \
    const clap_plugin_factory_t mclapFactory = {mclapCount, mclapDesc,          \
                                                mclapCreate};                   \
    bool CLAP_ABI mclapInit(const char* path) { return mclap::moduleInit(path); }\
    void CLAP_ABI mclapDeinit() { mclap::moduleDeinit(); }                       \
    const void* CLAP_ABI mclapGetFactory(const char* id) {                      \
        return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr                \
                                                       : &mclapFactory;         \
    }                                                                           \
    }                                                                           \
    extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {             \
        CLAP_VERSION_INIT, mclapInit, mclapDeinit, mclapGetFactory};
