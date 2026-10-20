// Stand-in for Dexed's own header.
//
// Two files in the Apache-2.0 msfa folder include "../Dexed.h", but the only
// thing they take from it is the TRACE macro, which is empty in a release
// build and which neither file actually calls. Supplying it here keeps the
// vendored engine to msfa alone - pulling in the real Dexed.h would mean
// taking the GPL-3.0 application code with it, and with it that licence.

#pragma once

#ifndef TRACE
#define TRACE(fmt, ...)
#endif
