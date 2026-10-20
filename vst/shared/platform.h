// The two things that are not the same on every machine.
//
// Everything else in these plug-ins is portable already - the DSP is plain
// C++, the SDK and VSTGUI abstract the host and the window. What is not
// portable is where a user's files live, and what the fonts are called.
//
// Both were written for Windows first and both fail quietly rather than
// loudly on a Mac: getenv("LOCALAPPDATA") returns null and the folder silently
// becomes a relative path next to the host application, and a request for
// "Consolas" is not an error - the system simply substitutes something else,
// so every numeral in every window would be set in a face nobody chose.

#pragma once

#include <string>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace mplat {

// Where this plug-in family keeps its own files: kits, cartridges, sets.
//
//   Windows   %LOCALAPPDATA%\<app>\<sub>
//   macOS     ~/Library/Application Support/<app>/<sub>
//   Linux     $XDG_DATA_HOME/<app>/<sub>, or ~/.local/share/<app>/<sub>
//
// Built with filesystem::path rather than by pasting separators into a string
// literal, for the reason recorded in every folder helper in this tree: a
// single backslash in a Windows path literal is not an escape sequence, and it
// is dropped with only a warning.
inline std::string userDataFolder(const char* app, const char* sub) {
    std::filesystem::path p;

#if defined(_WIN32)
    if (const char* base = std::getenv("LOCALAPPDATA")) p = base;
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        p = home;
        p /= "Library";
        p /= "Application Support";
    }
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) p = xdg;
    else if (const char* home = std::getenv("HOME")) { p = home; p /= ".local"; p /= "share"; }
#endif

    // A null environment variable is not a reason to write into whatever
    // directory the host happens to have been launched from.
    if (p.empty()) {
        if (const char* home = std::getenv("HOME")) p = home;
        else p = ".";
    }

    if (app && *app) p /= app;
    if (sub && *sub) p /= sub;

    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

// Faces that actually exist on each system.
//
// The look depends on a monospaced face for anything that is a name or a
// number - it is what keeps a column of values from dancing as they change -
// so the substitute has to be genuinely monospaced, not merely present.
#if defined(_WIN32)
static const char* const kMonoFont = "Consolas";
static const char* const kUiFont   = "Arial";
#elif defined(__APPLE__)
static const char* const kMonoFont = "Menlo";
static const char* const kUiFont   = "Helvetica Neue";
#else
static const char* const kMonoFont = "DejaVu Sans Mono";
static const char* const kUiFont   = "DejaVu Sans";
#endif

} // namespace mplat
