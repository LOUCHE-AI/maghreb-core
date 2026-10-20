#include "ui_scale.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#endif

namespace mplat {
namespace {

constexpr double kMinScale = 0.5;
constexpr double kMaxScale = 4.0;

double clampScale(double s) {
    if (!(s > 0.0)) return 0.0;            // also rejects NaN
    if (s < kMinScale) return kMinScale;
    if (s > kMaxScale) return kMaxScale;
    // Two decimals is finer than anyone can see and keeps the scaled window
    // size from landing on a long fraction.
    return std::round(s * 100.0) / 100.0;
}

double fromEnvironment() {
    const char* v = std::getenv("MAGHREB_UI_SCALE");
    if (!v || !*v) return 0.0;
    char* end = nullptr;
    const double s = std::strtod(v, &end);
    if (end == v) return 0.0;              // not a number at all
    return clampScale(s);
}

#if !defined(_WIN32) && !defined(__APPLE__)
// Xft.dpi as the desktop advertises it. Opens its own short-lived connection:
// this runs once, before any frame exists, so there is no VSTGUI connection to
// borrow yet.
double fromXResources() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return 0.0;

    double scale = 0.0;
    if (const char* rms = XResourceManagerString(dpy)) {
        XrmInitialize();
        if (XrmDatabase db = XrmGetStringDatabase(rms)) {
            char* type = nullptr;
            XrmValue val{};
            if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &val) && val.addr) {
                // val.addr is not guaranteed null-terminated at val.size.
                const std::string s(val.addr, val.size ? val.size - 1 : 0);
                const double dpi = std::strtod(s.c_str(), nullptr);
                if (dpi > 0.0) scale = clampScale(dpi / 96.0);
            }
            XrmDestroyDatabase(db);
        }
    }
    XCloseDisplay(dpy);
    return scale;
}
#endif

double resolve() {
    if (const double s = fromEnvironment(); s > 0.0) return s;
#if !defined(_WIN32) && !defined(__APPLE__)
    if (const double s = fromXResources(); s > 0.0) return s;
#endif
    return 1.0;
}

} // anonymous

double editorScale() {
    static const double scale = resolve();
    return scale;
}

} // namespace mplat
