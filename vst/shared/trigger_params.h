// The trigger buttons of shared/triggers.h, registered with the host.
//
// Kept apart from triggers.h so that one stays free of the SDK and can be
// tested on its own.

#pragma once

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/fstrdefs.h"

namespace mplat {

// One on/off parameter. Its id comes after every saved parameter, so a
// plug-in's state code never sees it; where it is registered among the others
// is up to the plug-in's knob banks.
inline void addTriggerParam(Steinberg::Vst::ParameterContainer& pc, int id, const char* name) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    char16 title[128];
    str8ToStr16(title, name, 128);
    pc.addParameter(title, nullptr, 1, 0.0, ParameterInfo::kCanAutomate, id);
}

} // namespace mplat
