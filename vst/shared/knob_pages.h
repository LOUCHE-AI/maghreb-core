// How a controller's eight knobs see a plug-in: its parameters in banks of
// eight.
//
// One list per plug-in does two jobs. Its order is the order the parameters
// are registered in, which is all Ableton Live goes by - Live gives a
// Launchkey in Plugin mode, or a Launchpad in Device mode, a plug-in's
// parameters eight at a time in exactly that order, on Windows and macOS as on
// Linux. And the CLAP wrapper hands the same banks to Bitwig by name, as
// Remote Controls pages.
//
// So every bank but the last has to be a full eight, with nothing repeated: a
// gap or a repeat in the middle would shift every bank after it in Live. The
// check in shared/test_clap_host.cpp holds the registered order to that.
//
// Parameters left off the banks are registered after them, in id order. Only
// hidden ones belong there - Live does not show those, so they cost no knob.

#pragma once

#include <cstddef>
#include <vector>

namespace mplat {

struct KnobPage {
    const char* name;
    int ids[8];          // -1 only to end the last bank early
};

// Registration order for ids 0..total-1: the banks first, then the rest.
template <size_t N>
std::vector<int> knobOrder(const KnobPage (&pages)[N], int total) {
    std::vector<int> order;
    std::vector<bool> seen((size_t)total, false);
    for (const KnobPage& pg : pages)
        for (int id : pg.ids)
            if (id >= 0 && id < total && !seen[(size_t)id]) {
                seen[(size_t)id] = true;
                order.push_back(id);
            }
    for (int id = 0; id < total; id++)
        if (!seen[(size_t)id]) order.push_back(id);
    return order;
}

} // namespace mplat
