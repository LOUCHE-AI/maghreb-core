// Opening the editor frame, which does not take the same arguments everywhere.
//
// On Windows and macOS a CFrame opens with nothing but the parent handle: the
// platform layer already has its own connection to the window server. On Linux
// it has none. VSTGUI's X11 backend draws over an xcb connection that it opens
// only when CFrame::open is handed an X11::FrameConfig carrying the host's run
// loop - see X11::Frame::Frame in vstgui/lib/platform/linux/x11frame.cpp, where
// the call to RunLoop::init() sits behind exactly that check.
//
// Open the frame without one and nothing complains. The connection stays null,
// and the first thing the new frame does with it is xcb_generate_id(), which
// dereferences it. In a host that runs plug-ins out of process - Bitwig does -
// that is a segfault in the plug-in process a fifth of a second after the
// window is asked for, reported as "Plug-in crashed" with no other detail.
//
// The run loop belongs to the host and reaches us as IPlugFrame. The SDK
// already knows how to wrap one for VSTGUI, so this only has to make sure that
// has happened and hand the result to CFrame::open.

#pragma once

#include "vstgui/lib/vstguifwd.h"
#include "pluginterfaces/gui/iplugview.h"

namespace mplat {

// Opens `frame` on `parent`. Returns false - rather than crashing - if the
// host gave us no run loop to draw against.
bool openEditorFrame(VSTGUI::CFrame* frame, void* parent,
                     const VSTGUI::PlatformType& type,
                     Steinberg::IPlugFrame* plugFrame);

// Call from every editor's close(), after the frame has been forgotten.
//
// The run loop VSTGUI holds is one per process, and when it is ours it was
// built around the IPlugFrame of whichever editor happened to open first. That
// editor eventually closes, and the run loop is left pointing into it: the next
// editor to open registers its timers and its X11 event handler against a frame
// that no longer exists. What that looks like from the outside is a plug-in
// window that appears and then vanishes, or one that draws but answers no
// clicks, or - when a timer fires into the dead frame - the whole plug-in host
// process going down and taking every other instance with it. Reloading the
// plug-in appears to fix it, because a fresh process starts the cycle again.
//
// So the count is kept, and when the last editor in the process closes, the run
// loop we installed is dropped. The next one to open installs its own, from a
// frame that is alive. A run loop the host installed itself is left alone - it
// is not ours to clear, and it outlives any one editor by design.
void closeEditorFrame();

} // namespace mplat
