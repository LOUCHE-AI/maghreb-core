// Starting points, by style.
//
// A corrector is mostly its settings: amount, how fast it gets there, and how
// much of a lean it tolerates. Those three are the difference between the
// stepped raï vocal and a correction nobody notices, so the presets are mostly
// about them.
//
// The ṭabʿ in each is a starting point, not a claim about repertoire. Which
// ṭabʿ a song is in is the singer's business and changes song to song; what
// these do is put you in the right kind of correction with a sensible mode
// already loaded, so the first thing you change is the ṭabʿ and not six
// numbers.
//
// Two of them exist to be compared:
//
//   Raï Hard      Sika, four degrees twelve-tone cannot reach
//   Twelve-Tone   Zidan, none - which is what an ordinary corrector does
//
// Switching between those two on the same take is the whole argument for this
// plug-in, audible in one click.

#pragma once
#include "tune_dsp.h"

namespace mtune {

struct Preset {
    const char* name;
    int   tab;          // index into tubu::kNames
    int   tonic;        // MIDI note
    float amount;       // 0..1
    float retuneMs;
    float deadCents;
    float mix;          // 0..1
    const char* note;
};

static const Preset kPresets[] = {
    { "Rai Hard", 3, 60, 1.00f, 0.f, 0.f, 1.00f,
      "The stepped raï vocal: full amount, no glide, no tolerance. You hear the "
      "note arrive on the degree. Sika has four degrees twelve-tone cannot reach." },

    { "Rai Modern", 3, 60, 1.00f, 12.f, 0.f, 1.00f,
      "The same snap with twelve milliseconds of glide - still obviously tuned, "
      "but the attack of each word is not quite so brittle." },

    { "Cheb Lead", 15, 60, 1.00f, 0.f, 6.f, 1.00f,
      "Hard, on Ghrib, but a lean of up to six cents is left alone so the smallest "
      "inflections into a degree survive the snap." },

    { "Rai Half", 3, 60, 0.55f, 18.f, 0.f, 1.00f,
      "Half the correction. The effect is plainly there and the singer is still "
      "audibly doing the work." },

    { "Chaabi Natural", 7, 57, 0.70f, 70.f, 20.f, 1.00f,
      "Corrects where a note lands and leaves how it was approached. Ramal keeps "
      "a whole second but a neutral third." },

    { "Andalusi Sanaa", 17, 57, 0.50f, 110.f, 30.f, 1.00f,
      "Gentle, on Sika Mahjuba - five neutral degrees including a neutral fifth. "
      "Slow enough that melisma passes through unflattened." },

    { "Malhun Gentle", 4, 55, 0.40f, 130.f, 35.f, 1.00f,
      "The lightest of them. Iraq, a wide deadzone, and a glide long enough that "
      "only a note that truly sits wrong is moved." },

    { "Parallel Hard", 3, 60, 1.00f, 0.f, 0.f, 0.50f,
      "Hard tuning underneath the dry voice rather than instead of it. The dry is "
      "delayed to meet it, so this is a blend and not a comb filter." },

    { "Tuner Only", 3, 60, 0.00f, 0.f, 0.f, 1.00f,
      "Corrects nothing. The display still shows where the voice sits against the "
      "ṭabʿ, which is the fastest way to find what a take is actually in." },

    { "Twelve-Tone", 13, 60, 1.00f, 0.f, 0.f, 1.00f,
      "What an ordinary corrector does: Zidan has no neutral degrees, so this is "
      "a plain chromatic snap. Here to be A/B'd against Rai Hard on the same take." },
};

static const int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

} // namespace mtune
