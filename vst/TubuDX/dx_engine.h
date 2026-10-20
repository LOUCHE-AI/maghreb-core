// A wall between msfa and everything else.
//
// This copy carries one addition over the Maghreb DX version: the engine's
// tuning is handed in as a table of 128 log-frequencies. That is the hook
// that makes a microtonal DX7 possible - msfa asks a TuningState for
// midinote_to_logfreq and uses the answer for every operator, so placing a
// tab's neutral degrees is a matter of filling in the table rather than
// bending anything.
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

    // Notes are held here, unlike the drum build: these are melodic patches.
    void noteOff(int note);
    void allOff();
    int  activeVoices() const;

    // The peak this patch reaches on its own, already through the engine trim,
    // so a caller can work out the gain that brings it to a sensible level.
    // Rendered on a scratch voice: the live pool is untouched, and it is meant
    // to be called when a patch is chosen rather than while one is playing.
    float patchPeak(const uint8_t patch[156], int note, double seconds = 0.7) const;

    // One operator's own output, for the patch page's waveform displays: the
    // real patch with every other operator silenced and the algorithm forced
    // to 32, so what is drawn is what msfa actually produces.
    bool renderOperator(const uint8_t patch[156], int op, int note,
                        float* out, int n) const;

    // How the six operators are wired, read out of msfa's own table. Indices
    // are msfa's: 0 is OP6 and 5 is OP1.
    static void algorithmRouting(int algorithm, int target[6], bool carrier[6],
                                 int& fbOp);

    // log2(frequency) * (1 << 24), one per MIDI note - msfa's own units.
    void setTuningTable(const int32_t logfreq[128]);

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
