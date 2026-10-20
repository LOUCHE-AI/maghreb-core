// A wall between msfa and everything else.
//
// msfa/synth.h defines min and max as templates in the global namespace. That
// is harmless on its own, but the VST3 SDK and VSTGUI pull in std::max, and
// once both are visible in one translation unit every call is ambiguous and
// the vendored engine stops compiling.
//
// Rather than patch someone else's source, msfa is confined to dx_engine.cpp,
// which includes the SDK not at all. Everything above this header talks to the
// engine through the handful of calls below and never sees a DX7 type. It is
// also the cleaner arrangement regardless: the plug-in depends on an
// interface rather than on Dexed's internals, so a future msfa update cannot
// reach further than this file.

#pragma once
#include <cstdint>
#include <memory>

namespace mdx {

class DxEngine {
public:
    DxEngine();
    ~DxEngine();

    // Rebuilds msfa's tables. Those are static and therefore shared by every
    // instance in the host, which is fine because a host runs them all at one
    // sample rate.
    void setSampleRate(double sr);

    // Starts a voice on a derived patch. `stroke` is carried only so the
    // display can be told which pad lit.
    // `group` ties a note to a voice slot that only one note may hold at a
    // time: starting another in the same group releases the one before it.
    // A drum lane put on a sustained cartridge voice otherwise stacks a dozen
    // ringing copies of itself and the sum clips - measured at 1.8 before
    // this existed. -1 means ungrouped, which is how the drums behave.
    void noteOn(const uint8_t patch[156], int note, int velocity,
                float pan, float gain, int stroke, int group = -1);

    void allOff();
    int  activeVoices() const;

    // The peak this patch reaches on its own, already through the engine trim,
    // so a caller can work out the gain that brings it to a sensible level.
    // Rendered on a scratch voice: the live pool is untouched, and it is meant
    // to be called when a patch is chosen rather than while one is playing.
    float patchPeak(const uint8_t patch[156], int note, double seconds = 0.7) const;

    // Adds into L and R. msfa renders in fixed 64-sample blocks, so the
    // residual is kept here and n may be anything.
    void render(float* L, float* R, int n);

    // Peak of each stroke during the last render, for the pads. Cleared by
    // the call that reads it.
    void takeStrokePeaks(float* out, int count);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mdx
