// Getting an audio file that already exists into a plug-in.
//
// Two paths, and the split is deliberate. A WAV is read here, in process, with
// no external anything - which is the common case and the one that must never
// depend on what else is installed. Everything else - mp3, m4a, ogg, flac -
// goes through ffmpeg, because writing four more decoders to save one process
// is not a good trade, and ffmpeg reads formats this will never have heard of.
//
// Rate conversion is done here either way, with a windowed sinc rather than
// the linear interpolation the sampler in this family uses. Linear is fine for
// grains, where the material is about to be chopped up anyway. It is not fine
// for a transfer that is meant to be an archive: downsampling with it folds
// everything above the new Nyquist back into the audible band as aliasing,
// which is exactly the sort of damage this family of plug-ins exists to undo.

#pragma once

#include <string>
#include <vector>

namespace maudio {

// Is ffmpeg reachable? False means compressed formats will be refused with an
// explanation rather than failing obscurely.
bool haveFfmpeg();

// Everything readable, listed for a file dialog.
extern const char* const kExtensions[];
extern const int kNumExtensions;

// Reads `path` into planar float at `targetRate`, resampling if it has to.
// Channels come back as the file has them.
bool importAudio(const std::string& path,
                 std::vector<std::vector<float>>& out,
                 double targetRate,
                 std::string* err = nullptr);

// Band-limited rate conversion. Exposed because it is the part worth testing
// on its own - a sine that comes back a different shape is easy to see.
void resample(std::vector<std::vector<float>>& channels, double from, double to);

} // namespace maudio
