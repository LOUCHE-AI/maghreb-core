// How big a pixel is, which the plug-in has to work out for itself.
//
// These editors are laid out in fixed pixels - 764 wide for Maghreb DX, 900
// for the ones with a cartridge list - because that is what the drawing code
// wants: a row of eight cells is eight cells wide, not a fraction of whatever
// the window happens to be. On the 96 dpi screens they were drawn on that is
// the right size. On a 4K panel at 170% it is a postage stamp.
//
// VST3 has an interface for this - the host calls setContentScaleFactor and
// the plug-in scales itself - but a host has to choose to use it. Bitwig on
// Linux starts its plug-in host with the flag "not-dpi-aware" and never asks,
// so a plug-in that waits to be told stays small forever.
//
// What is reliable here is the X resource database. Under KDE, XWayland
// clients get Xft.dpi set from the compositor's scale, and the plug-in host
// process reads the same display as everything else - so 163 dpi means the
// desktop is running at roughly 170% and the editor should be too.

#pragma once

namespace mplat {

// The factor editors should zoom by. Resolved once, then cached.
//
// In order: MAGHREB_UI_SCALE if it is set and sane, then the display's own
// Xft.dpi over 96, then 1.0. Clamped to [0.5, 4.0] so a stray environment
// variable cannot produce a window with no close button on screen.
double editorScale();

} // namespace mplat
