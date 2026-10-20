// The trigger buttons of shared/triggers.h, registered with the host.
//
// Kept apart from triggers.h so that one stays free of the SDK and can be
// tested on its own.

#pragma once

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/fstrdefs.h"

namespace mplat {

// On/off parameters with ids firstId, firstId + 1, ... - after every saved
// parameter, so a plug-in's state code never sees them.
inline void addTriggerParams(Steinberg::Vst::ParameterContainer& pc, int firstId,
                             const char* const* names, int n) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    for (int i = 0; i < n; i++) {
        char16 title[128];
        str8ToStr16(title, names[i], 128);
        pc.addParameter(title, nullptr, 1, 0.0, ParameterInfo::kCanAutomate, firstId + i);
    }
}

} // namespace mplat
