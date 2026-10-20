// The ceiling a polyphonic instrument needs and a monophonic one does not.
//
// These voices are each normalised on their own - a cartridge patch is scaled
// at load so one note peaks around half - but six of them held together still
// sum past 1.0, and a host does not ask nicely about that: it clips, and what
// you hear is harsh distortion that gets worse the more notes you play.
//
// So the output is shaped rather than cut. Below the knee nothing happens at
// all, sample for sample, which matters because most playing never reaches it.
// Above it the remaining headroom is compressed with a tanh, so the signal
// approaches 1.0 and never crosses it however many voices pile in.
//
// A hard clamp would also stop the overs, and would sound like the problem it
// was fixing.

#pragma once

#include <cmath>

namespace mdsp {

// `knee` is where shaping begins. 0.7 leaves the normal playing range
// untouched and still has room to bend the rest into.
inline float softClip(float x, float knee = 0.7f) {
    const float a = std::fabs(x);
    if (a <= knee) return x;
    const float over = (a - knee) / (1.f - knee);
    const float shaped = knee + (1.f - knee) * std::tanh(over);
    return std::copysign(shaped, x);
}

inline void softClipBlock(float* L, float* R, int n, float knee = 0.7f) {
    for (int i = 0; i < n; i++) { L[i] = softClip(L[i], knee); R[i] = softClip(R[i], knee); }
}

} // namespace mdsp
