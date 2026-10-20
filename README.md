# maghreb-instruments

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
[latest release](https://github.com/LOUCHE-AI/maghreb-instruments/releases/latest),
unzip it, and copy the plug-ins to where your DAW looks:

| System  | VST3 goes in | CLAP goes in |
|---------|--------------|--------------|
| Linux   | `~/.vst3` | `~/.clap` |
| Windows | `C:\Program Files\Common Files\VST3` | – |
| macOS   | `~/Library/Audio/Plug-Ins/VST3` | – |

**macOS:** the plug-ins are not signed by Apple, so macOS blocks them at
first. After copying, run this once in Terminal:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Maghreb*.vst3 ~/Library/Audio/Plug-Ins/VST3/Tape*.vst3 ~/Library/Audio/Plug-Ins/VST3/TubuDX.vst3

## Status

Built and used daily on Linux (Bitwig 6, Audacity 4). The Windows and macOS
builds are made automatically on every change, but nobody has tried them in a
DAW yet. Reports of what works and what doesn't are very welcome.

Known gaps outside Linux:

- CLAP is Linux-only: its editor is embedded through X11.
- TapeGrain's stem separation and TapeRepair's MP3/FLAC import call out to
  Demucs and ffmpeg; on Windows that is not wired up yet, so stems are off and
  TapeRepair imports WAV only.

## Building (Linux)

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
`-DCMAKE_CXX_FLAGS=-I/path/to/clap/include` to the first cmake line.

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
trips its state and opens and reopens its editor.

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
