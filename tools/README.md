# tools

## tubu_scales.py

Exports the eighteen ṭubūʿ so they can be played outside the plug-ins.

    python3 tubu_scales.py              # write them under tools/tunings
    python3 tubu_scales.py --install    # and copy the .scl where Dexed looks
    python3 verify_tunings.py           # read them back and check them

There is nothing to install for the script itself: one file, standard library
only. `--install` copies the Scala files to
`~/.local/share/DigitalSuburban/Dexed/Tunings`, which is where Dexed's own file
dialog opens. The Micro-pitch tables are not installed anywhere because Bitwig
has no folder to put them in - they are typed in by hand.

The source of truth is `vst/MaghrebTubu/tubu_tuning.h` and nothing else — the
tunings are parsed out of it rather than copied, so there is no second table to
fall out of step. Twenty-six of the degrees across the eighteen are neutral
intervals that 12-TET has no key for, which is the whole reason any of this
exists.

### What Bitwig can and cannot do with them

Checked against Bitwig 6.0 on this machine rather than assumed:

- **The controller API has no tuning of any kind.** There is no scale or
  microtuning object, so a controller script cannot install these. That route
  is closed.
- **Bitwig does not read Scala files.** No `.scl` or `.kbm` anywhere in the
  application. (It is full of the string "Scala", but that is the language it
  is partly written in.)
- **Bitwig does have a Micro-pitch device** — twelve pitch classes, each with a
  cent offset. That is enough to express any of these, because no two degrees
  of any ṭabʿ land on the same key. It has to be typed in by hand: presets are
  a binary format.

So there are two routes, and which you want depends on what is making the sound.

### scl/ — for plug-ins inside Bitwig

Standard Scala files. Dexed reads them, and so does most anything microtonal.
This is the route that actually sounds right: the tuning is exact, not rounded
to the nearest key with an offset.

Pair with `tubu.kbm` so seven degrees map to seven keys — without it the eighth
key gives you the octave a fifth early.

### micropitch/ — for Bitwig's own device

One text file per ṭabʿ, listing the offsets to dial into Micro-pitch. Only the
keys that move are listed; everything else stays at 0 and remains 12-TET.

A neutral second is written as a lowered D rather than a raised C♯, and a
neutral third as a lowered E — so 150 cents is `D -50` and 350 is `E -50`,
which is how these are notated.

### The ones that need nothing

Seven of the eighteen — Dhil, Rasd, Rasd al-Dhil, Hsin Ashiran, Maya, Isbahan,
Zidan — are entirely 12-TET. Their Micro-pitch files say so and their `.scl`
files are still exact.

Mazmum has five distinct degrees rather than seven: its fourth and fifth are
both 700 and its sixth and seventh both 900. That is what the Move's own table
holds, and `tubu_tuning.h` explains why it is left alone.
