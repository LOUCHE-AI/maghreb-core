// The four things wrong with nearly every tape transfer, and undoing them.
//
// These are the same algorithms the tapedeck archiver runs, and deliberately
// the offline ones rather than realtime approximations of them. A plug-in that
// repairs a recording it has already captured is under no obligation to work a
// block at a time, and the offline versions are better: the hum notch runs
// forwards and then backwards so it contributes no phase shift at all, and the
// noise floor is measured from a stretch of the actual recording rather than
// guessed at from what has gone past.
//
// Order matters, and repair() applies them in this one for a reason:
//
//   1. DC offset - a capture chain sits the whole waveform off centre.
//                  First, so nothing downstream measures a level that is
//                  partly just the offset.
//   2. Hum       - mains and its harmonics. Before the noise floor is
//                  measured, so a 50 Hz spike is not taken for part of the
//                  hiss and subtracted everywhere.
//   3. Hiss      - the tape. Spectral subtraction against a measured profile.
//   4. Level     - last, because everything above changes the peak.
//
// No external FFT: a radix-2 transform of a couple of thousand points is
// sixty lines, and a plug-in that drags in libfftw is a plug-in that stops
// loading on a machine which does not have it.

#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace trestore {

// Planar, one vector per channel - every stage below works a channel at a
// time, and a side of tape is small enough to hold whole.
struct Clip {
    std::vector<std::vector<float>> ch;
    double sampleRate = 48000.0;

    int channels() const { return (int)ch.size(); }
    size_t frames() const { return ch.empty() ? 0 : ch[0].size(); }
    bool empty() const { return frames() == 0; }
    double seconds() const { return sampleRate > 0 ? (double)frames() / sampleRate : 0.0; }
};

double linearToDb(double linear);
double dbToLinear(double db);
float peak(const Clip& c);

// Subtracts each channel's own mean. Per channel, not a shared figure - the
// two sides of a capture chain rarely drift by the same amount.
void removeDcOffset(Clip& c);

struct HumOptions {
    double baseHz    = 50.0;   // 60 in the Americas and Japan
    int    harmonics = 4;      // 50, 100, 150, 200
    double q         = 30.0;   // narrow enough to leave a bass line alone
};

// Notches the mains frequency and its harmonics, forwards then backwards so
// the filter adds no phase shift of its own.
void removeHum(Clip& c, const HumOptions& o = {});

// The average spectrum of a stretch that is supposed to be silent.
struct NoiseProfile {
    std::vector<float> mag;      // mean magnitude per bin
    int    fftSize    = 2048;
    double sampleRate = 0.0;
    bool valid() const { return !mag.empty(); }
};

// Measures from `startSec` for `lenSec` - point it at a gap between phrases,
// or the run-in before the music starts.
NoiseProfile measureNoise(const Clip& c, double startSec, double lenSec,
                          int fftSize = 2048);

// Finds the quietest stretch of `lenSec` and measures there. What you want
// when there is no obvious run-in to point at, and what the plug-in's LEARN
// button uses when nothing is selected.
NoiseProfile measureQuietest(const Clip& c, double lenSec = 1.0,
                             int fftSize = 2048);

// How a long operation reports in, and how it is stopped.
//
// Called from whichever thread is doing the work, so it must be cheap and must
// not block. `fraction` runs 0 to 1. Return false to abandon the run: the clip
// is then left partly processed, so a caller that cancels must throw it away.
// That is no burden here - everything offline works on a copy of the take.
using Progress = std::function<bool(double fraction)>;

struct HissOptions {
    float amount  = 1.0f;    // multiples of the measured floor to remove
    float floorDb = -18.f;   // the most any one bin may be pulled down
    // How many cores to spread the transforms over. 0 asks the machine. 1
    // forces the old single-threaded path, which is what the tests compare
    // against - the two must agree sample for sample.
    int   threads = 0;
};

// Spectral subtraction. floorDb is what keeps this from sounding like a
// swimming pool: bins are attenuated towards a floor, never to nothing, so
// what is left of the hiss stays steady instead of gating around the music.
// Returns false if it could not run at all, and also if `onProgress` asked it
// to stop. This is the slow one - about 1.3 seconds per minute of stereo tape -
// so it is the only stage that reports progress, and the only one worth
// cancelling.
bool reduceHiss(Clip& c, const NoiseProfile& p, const HissOptions& o = {},
                const Progress& onProgress = {});

// Scales so the loudest sample lands on targetPeakDb.
void normalize(Clip& c, double targetPeakDb = -1.0);

// ---------------------------------------------------------------- sides
//
// A side of tape arrives as one recording and is wanted as its songs. What
// separates them is not silence - tape hiss is never silent, and the machine
// is still running - but a stretch quiet enough, and long enough, that nobody
// would call it part of the music.

struct TrackOptions {
    double gapDb      = -45.0;  // quieter than this counts towards a gap
    double minGapSec  = 2.0;    // and a gap has to last this long to be one
    double minTrackSec = 20.0;  // anything shorter is a false start, not a song
    double windowSec  = 0.05;   // how finely the level is measured
};

struct Track {
    double startSec = 0.0;
    double endSec   = 0.0;
    double seconds() const { return endSec - startSec; }
};

// Where the songs are. Empty if the clip is empty; one track covering the
// whole thing if no gap in it is long enough to divide anything.
//
// The boundaries land on the music, not in the middle of the gap: each track
// starts where it gets loud and ends where it stops, so exporting them writes
// songs rather than songs wrapped in silence.
std::vector<Track> findTracks(const Clip& c, const TrackOptions& o = {});

// One track lifted out as its own clip.
Clip extract(const Clip& c, const Track& t);

} // namespace trestore
