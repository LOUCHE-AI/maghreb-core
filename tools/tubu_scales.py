#!/usr/bin/env python3
"""Export the eighteen tubu'  tunings for use outside the plug-ins.

The source of truth is vst/MaghrebTubu/tubu_tuning.h and nothing else. The
tunings are parsed out of it rather than copied, so a change to the header
reaches everything this writes the next time it is run, and there is no second
copy to fall out of step.

What comes out, and why each:

  scl/*.scl    Scala tuning files. The universal format - Dexed reads them,
               and so does most anything microtonal. This is how the neutral
               degrees actually sound right in a plug-in inside Bitwig.

  micropitch/  A table per tabʿ of the twelve cent offsets to dial into
               Bitwig's own Micro-pitch device. Bitwig has no Scala import
               and its controller API has no tuning at all, so Micro-pitch
               is the native route and it has to be entered by hand.

  tubu.kbm     A Scala keyboard map. The tubuʿ have seven degrees and a
               keyboard has twelve keys per octave; without this a player
               reaches for the eighth key and gets the octave a fifth early.

Run:  python3 tubu_scales.py            write them next to this script
      python3 tubu_scales.py --install  and copy them where Dexed will find them

Nothing to install for the script itself - it is one file and uses only the
standard library.
"""

import argparse
import os
import pathlib
import re
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent
HEADER = HERE.parent / "vst" / "MaghrebTubu" / "tubu_tuning.h"


def parse_header(path):
    """Pulls the names and the cent table out of the C++ header."""
    text = path.read_text()

    m = re.search(r"kNames\[kNumTubu\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit("could not find kNames in " + str(path))
    names = re.findall(r'"([^"]+)"', m.group(1))

    m = re.search(r"kFactory\[kNumTubu\]\[kDegrees\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("could not find kFactory in " + str(path))
    body = re.sub(r"//[^\n]*", "", m.group(1))          # the table is heavily commented
    rows = re.findall(r"\{([^}]*)\}", body)
    table = [[int(v) for v in re.findall(r"-?\d+", r)] for r in rows]

    if len(names) != len(table):
        raise SystemExit(f"{len(names)} names but {len(table)} rows - the header moved")
    return names, table


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def write_scl(path, name, cents):
    """One Scala file. Degrees are pitches above the tonic; the octave closes
    the scale and is the last line, which is what the format expects."""
    steps = sorted(set(c for c in cents if c > 0))
    steps.append(1200)                                   # the octave closes it
    lines = [
        f"! {path.name}",
        f"! Generated from tubu_tuning.h - do not edit by hand.",
        "!",
        f"{name} - tabʿ from Tunisian maʾluf / Algerian sanʿa",
        f" {len(steps)}",
        "!",
    ]
    lines += [f" {c:.5f}" for c in steps]
    path.write_text("\n".join(lines) + "\n")
    return steps


def micropitch(cents):
    """The twelve cent offsets Bitwig's Micro-pitch wants.

    Each degree is placed on its nearest semitone and the remainder becomes
    that key's offset. Ties go up, which is how these are written: a neutral
    second is a lowered D rather than a raised C sharp, and a neutral third a
    lowered E - so 150 is D minus 50 and 350 is E minus 50.

    Keys the tabʿ does not use are left at zero. They are still 12-TET, which
    is honest: the scale has seven degrees and the keyboard has twelve.
    """
    offsets = [0] * 12
    used = {}
    clashes = []
    for c in sorted(set(cents)):
        semi = int((c + 50) // 100) % 12                 # ties upward
        off = c - 100 * int((c + 50) // 100)
        if semi in used and used[semi] != off:
            clashes.append((semi, used[semi], off))
        used[semi] = off
        offsets[semi] = off
    return offsets, clashes


NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE / "tunings"))
    ap.add_argument("--install", action="store_true",
                    help="also copy the Scala files where Dexed's dialog opens")
    args = ap.parse_args()

    names, table = parse_header(HEADER)
    out = pathlib.Path(args.out)
    (out / "scl").mkdir(parents=True, exist_ok=True)
    (out / "micropitch").mkdir(parents=True, exist_ok=True)

    print(f"{len(names)} tubuʿ from {HEADER.relative_to(HERE.parent)}\n")

    report = []
    neutral_total = 0
    for name, cents in zip(names, table):
        distinct = sorted(set(cents))
        neutral = [c for c in distinct if c % 100]
        neutral_total += len(neutral)

        s = out / "scl" / f"{slug(name)}.scl"
        write_scl(s, name, cents)

        offs, clashes = micropitch(cents)
        rows = [f"{NOTE[i]:<3} {offs[i]:+d}" for i in range(12) if offs[i]]
        (out / "micropitch" / f"{slug(name)}.txt").write_text(
            f"{name} - Bitwig Micro-pitch offsets, in cents\n"
            f"Generated from tubu_tuning.h.\n\n"
            f"degrees: {', '.join(str(c) for c in distinct)}\n\n"
            + ("\n".join(rows) if rows else "(no offsets - this tabʿ is plain 12-TET)")
            + "\n\nEvery key not listed stays at 0.\n")

        flag = ""
        if len(distinct) < len(cents):
            flag = f"  ({len(distinct)} distinct degrees, not {len(cents)})"
        if clashes:
            flag += "  CLASH: two degrees want the same key"
        report.append((name, len(neutral), flag))

    # the keyboard map: seven degrees over twelve keys
    (out / "tubu.kbm").write_text(
        "! tubu.kbm - seven degrees to the octave\n"
        "! Generated from tubu_tuning.h.\n"
        "7\n0\n127\n60\n69\n440.0\n7\n0\n1\n2\n3\n4\n5\n6\n")

    w = max(len(n) for n, _, _ in report)
    for name, n_neutral, flag in report:
        mark = f"{n_neutral} neutral" if n_neutral else "12-TET only"
        print(f"  {name:<{w}}  {mark}{flag}")

    print(f"\n{neutral_total} degrees across the eighteen that 12-TET cannot play.")
    print(f"written to {out}")
    print("\n  scl/         -> Dexed, Surge, anything Scala-aware, inside Bitwig")
    print("  micropitch/  -> type into Bitwig's own Micro-pitch device")
    print("  tubu.kbm     -> pair with the .scl so seven degrees map to seven keys")

    if args.install:
        # Beside Dexed's own cartridges, because that is where its file dialog
        # starts and a tuning nobody can find is a tuning nobody uses.
        share = os.environ.get("XDG_DATA_HOME") or (pathlib.Path.home() / ".local" / "share")
        dest = pathlib.Path(share) / "DigitalSuburban" / "Dexed" / "Tunings"
        dest.mkdir(parents=True, exist_ok=True)
        n = 0
        for f in sorted((out / "scl").glob("*.scl")):
            shutil.copy2(f, dest / f.name)
            n += 1
        shutil.copy2(out / "tubu.kbm", dest / "tubu.kbm")
        print(f"\ninstalled {n} scales and the keyboard map to")
        print(f"  {dest}")
        print("\nin Dexed: the tuning button, Load Scala (.scl) - and Load Keyboard")
        print("Mapping (.kbm) with tubu.kbm if you want seven degrees on seven keys.")


if __name__ == "__main__":
    main()
