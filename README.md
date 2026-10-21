# Maghreb Core

Eight audio plug-ins, VST3 and CLAP, built around Maghrebi rhythm and tuning
and around cassette tape. They started as modules for the Ableton Move and grew
into desktop plug-ins.

| Plug-in      | What it does |
|--------------|--------------|
| MaghrebDX    | Eleven Maghrebi strokes on a six-operator FM engine, a pad each. |
| MaghrebFM    | The same eleven strokes on two operators. |
| MaghrebSeq   | Ninety rhythms on four lanes with a song chain; MIDI out, MIDI file, loop and kit export. |
| MaghrebTubu  | The eighteen ṭubūʿ (Maghrebi modes), played rather than corrected to. |
| MaghrebTune  | Pitch correction to a degree of the ṭabʿ, including the neutral intervals 12-TET cannot reach. Live and offline modes. |
| TapeGrain    | A cloud of grains off a sample, with a stem per slot (stems via Demucs, optional). |
| TapeRepair   | Record a tape transfer, mend it offline (hum, hiss, DC offset, level), split a side into tracks. |
| TubuDX       | A DX7 voice per pad, tuned to a ṭabʿ rather than to twelve. |

## Download

Get the zip for your system from the
[latest release](https://github.com/LOUCHE-AI/maghreb-core/releases/latest),
unzip it, and copy the plug-ins to where your DAW looks:

| System  | Needs | VST3 goes in | CLAP goes in |
|---------|-------|--------------|--------------|
| Linux   | x86-64, glibc 2.35+ (Ubuntu 22.04, Debian 12 or newer) | `~/.vst3` | `~/.clap` |
| Windows | 64-bit Windows 10 or 11 | `C:\Program Files\Common Files\VST3` | – |
| macOS   | macOS 10.15 or newer, Intel or Apple silicon | `~/Library/Audio/Plug-Ins/VST3` | – |

**macOS:** the plug-ins are not signed by Apple, so macOS blocks them at
first. After copying, run this once in Terminal:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Maghreb*.vst3 ~/Library/Audio/Plug-Ins/VST3/Tape*.vst3 ~/Library/Audio/Plug-Ins/VST3/TubuDX.vst3

## Feedback and support

- **Something broken?** [Open an issue](https://github.com/LOUCHE-AI/maghreb-core/issues/new/choose) -
  say which plug-in, your system and DAW, and what happened.
- **An idea, a tip, a question, or something you made with them?** Post it in
  [Discussions](https://github.com/LOUCHE-AI/maghreb-core/discussions).
- **Like them?** They are free. If you want to support the work,
  there is [Ko-fi](https://ko-fi.com/louloulouche).

## Controllers

Every knob and switch is a host parameter, so any MIDI controller can be
mapped to it the usual way. Two things go further:

- **Knob banks.** Each plug-in's parameters come in banks of eight, so an
  eight-encoder controller turns them with no mapping - a Novation Launchkey
  in Plugin mode, or a Launchpad's Device mode. In Ableton Live (any system)
  that is the order the parameters are registered in, which Live hands the
  controller eight at a time; in Bitwig the CLAP versions give the same banks
  as named Remote Controls pages. Anything that could throw work away (Reset,
  Revert, Arm, Record, Preset) is in the last bank. MaghrebSeq's song-chain
  slots are hidden from the host, which keeps it under the 64 parameters Live
  will show without configuring; they are still saved with the Set.
- **Buttons.** What used to be only on screen is a parameter too, for a pad to
  press: Prev/Next Patch and Prev/Next Favourite (MaghrebDX, TubuDX,
  MaghrebSeq), Write MIDI, Render Loop, Render Kit (MaghrebSeq), Learn, Repair,
  Revert, Split Side, Send to TapeGrain (TapeRepair), From Tape (TapeGrain).
  They act on the press, not the release, so map pads as momentary. They work
  with the plug-in's window closed.

Patch steps go through every voice of every cartridge in the folder, in the
browser's order; favourite steps go through that plug-in's starred list.
MaghrebSeq's land on the lane last selected in its editor.

## Status

Built and used daily on Linux (Bitwig 6, Audacity 4). The Windows and macOS
builds are made automatically on every change and pass Steinberg's VST3
validator there, but nobody has played them in a DAW on those systems yet.
Reports of what works and what doesn't are very welcome.

Known gaps outside Linux:

- CLAP is Linux-only: its editor is embedded through X11.
- TapeGrain's stem separation and TapeRepair's MP3/FLAC import call out to
  Demucs and ffmpeg; on Windows that is not wired up yet, so stems are off and
  TapeRepair imports WAV only.

## Building

You need CMake ≥ 3.20, a C++17 compiler, the VST3 SDK's Linux dependencies
([Steinberg's list](https://steinbergmedia.github.io/vst3_dev_portal/pages/Getting+Started/How+to+setup+my+system.html#for-linux);
on Debian/Ubuntu roughly `libx11-xcb-dev libxcb-util-dev libxcb-cursor-dev
libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev libfontconfig1-dev
libcairo2-dev libgtkmm-3.0-dev libsqlite3-dev libxcb-keysyms1-dev`), and the
[CLAP headers](https://github.com/free-audio/clap) (Arch: `pacman -S clap`).

1. Fetch the SDK into `vst/sdk` (it is not in this repository):

       git clone --recursive --branch v3.8.1_build_84 https://github.com/steinbergmedia/vst3sdk.git vst/sdk

2. Apply the VSTGUI patch. Without it the host crashes the second time an
   editor opens on Linux; [vst/patches/README.md](vst/patches/README.md) says why.

       cd vst/sdk/vstgui4 && patch -p1 < ../../patches/vstgui-keep-x-connection.patch && cd -

3. Build a plug-in (each one is its own CMake project):

       cmake -S vst/MaghrebTune -B vst/MaghrebTune/build -DCMAKE_BUILD_TYPE=Release
       cmake --build vst/MaghrebTune/build -j

4. Copy the results where hosts look:

       cp -r vst/MaghrebTune/build/VST3/Release/MaghrebTune.vst3 ~/.vst3/
       cp vst/MaghrebTune/build/lib/Release/MaghrebTune.clap ~/.clap/

If the CLAP headers are not installed system-wide, add
`-DCLAP_INCLUDE_DIR=/path/to/clap/include` to the first cmake line; without
them only the VST3 is built.

On Windows (Visual Studio) and macOS (Xcode) the steps are the same without
step 2; on macOS add `-G Xcode` to the first cmake line. No CLAP is built
there. [.github/workflows/build.yml](.github/workflows/build.yml) is the exact
recipe for all three.

## Layout

- `vst/<Plugin>/` – one plug-in: DSP in `*_dsp.h`, VST3 wrapper `*_vst.cpp`,
  editor `*_editor.cpp`, CLAP entry `clap_entry.cpp`.
- `vst/shared/` – code the family shares: editor widgets, the cartridge
  browser, tape restoration, MIDI writing, and `clap_vst3.cpp`, which turns
  each VST3 component into a CLAP plug-in without touching its DSP or editor.
- `vst/patches/` – changes to the SDK, reapplied after fetching it.
- `tools/` – exports the ṭubūʿ as Scala (`.scl`) files for other synths.

## Tests

Every `test_*.cpp` is a standalone program with plain asserts, no framework.
From a plug-in's folder most build as

    g++ -O2 -std=c++17 -I. -I../shared test_something.cpp -o /tmp/t && /tmp/t

and a few also need the `.cpp` the linker names. `vst/shared/test_clap_host.cpp`
is a minimal CLAP host that loads a built `.clap`, runs audio through it, round-
trips its state and opens and reopens its editor. With `CLAP_TEST_PRESS=1` it also
presses the controller buttons; point `XDG_DATA_HOME` at a scratch folder with
a cartridge in `MaghrebDX/Cartridges` first, since pressing them writes files.

## DX7 cartridges

MaghrebDX, TubuDX and MaghrebSeq read DX7 `.syx` cartridges from
`~/.local/share/MaghrebDX/Cartridges` (searched recursively). None are bundled.

## Contributing

Issues and pull requests are welcome. Keep changes to one plug-in or one
shared file where possible, and add or extend a `test_*.cpp` when you change
DSP.

## Licence and credits

GPL-3.0, see [LICENSE](LICENSE). Code by others, with its licences and changes, is
listed in [vst/NOTICE.txt](vst/NOTICE.txt):

- [msfa](https://github.com/asb2m10/dexed) (Apache 2.0), the FM core of Dexed;
  only msfa is used, none of Dexed's GPL code.
- Steinberg's VST3 SDK (MIT) and VSTGUI (BSD 3-clause).
- MaghrebSeq's rhythms come from *Jas's Middle Eastern Rhythms List*.

VST is a trademark of Steinberg Media Technologies GmbH.
