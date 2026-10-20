#include "frame_open.h"

#include "vstgui/lib/cframe.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#endif

namespace mplat {
namespace {

// How many editor frames this process has open, and whether the run loop the
// platform factory is holding is one we installed rather than the host's.
// Editors open and close on the host's UI thread, so a plain int is enough.
int  gOpenFrames = 0;
bool gWeInstalledRunLoop = false;

} // namespace

bool openEditorFrame(VSTGUI::CFrame* frame, void* parent,
                     const VSTGUI::PlatformType& type,
                     Steinberg::IPlugFrame* plugFrame) {
    if (!frame) return false;

#if SMTG_OS_LINUX
    auto* factory = VSTGUI::getPlatformFactory().asLinuxFactory();
    if (!factory) return false;

    // The SDK installs the host's run loop from its own module initialiser,
    // but only for hosts that call setHostContext on the factory. Not every
    // host does, and the ones that do have already done it by now, so asking
    // again costs a null check and covers the ones that do not.
    if (factory->getRunLoop() == nullptr && plugFrame) {
        if (Steinberg::Linux::setupVSTGUIRunloop(plugFrame))
            gWeInstalledRunLoop = true;
    }

    VSTGUI::X11::FrameConfig config;
    config.runLoop = factory->getRunLoop();

    // This is the check that matters. A frame opened without a run loop never
    // opens its xcb connection and dies on first use of it. An editor that
    // declines to open is a window that does not appear; an editor that opens
    // anyway takes the host's plug-in process down with it.
    if (!config.runLoop) return false;

    if (!frame->open(parent, type, &config)) return false;
    ++gOpenFrames;
    return true;
#else
    (void)plugFrame;
    return frame->open(parent, type);
#endif
}

void closeEditorFrame() {
#if SMTG_OS_LINUX
    if (gOpenFrames > 0) --gOpenFrames;
    if (gOpenFrames > 0 || !gWeInstalledRunLoop) return;

    // The last one out. Drop the run loop we built around an IPlugFrame, so
    // nothing outlives the frame it was made from.
    if (auto* factory = VSTGUI::getPlatformFactory().asLinuxFactory())
        factory->setRunLoop(nullptr);
    gWeInstalledRunLoop = false;
#endif
}

} // namespace mplat
