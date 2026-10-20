#!/usr/bin/env python3
"""Reads the exports back and checks they reconstruct the header.

An export that silently drops a degree is worse than no export: it would sound
almost right. So every file is parsed again and compared with the source it
came from, rather than trusted because the writer looked correct.
"""
import pathlib, re, sys
from tubu_scales import parse_header, slug, micropitch, HEADER, NOTE

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "tunings"
fails = 0

def ok(label, cond, detail=""):
    global fails
    print(f"{'PASS ' if cond else 'FAIL '}  {label}{'  ' + detail if detail else ''}")
    if not cond: fails += 1

names, table = parse_header(HEADER)
ok("the header still parses", len(names) == 18 and len(table) == 18,
   f"{len(names)} tubu'")

for name, cents in zip(names, table):
    distinct = sorted(set(c for c in cents if c > 0))

    # ---- the Scala file ----
    p = OUT / "scl" / f"{slug(name)}.scl"
    if not p.exists():
        ok(f"{name}: scl exists", False); continue
    lines = [l.strip() for l in p.read_text().splitlines()
             if l.strip() and not l.strip().startswith("!")]
    declared = int(lines[1])                       # after the description line
    steps = [float(x) for x in lines[2:]]
    got = [s for s in steps if s < 1200]

    ok(f"{name}: scl count matches its lines", declared == len(steps),
       f"says {declared}, has {len(steps)}")
    ok(f"{name}: scl closes on the octave", steps and abs(steps[-1] - 1200) < 1e-6)
    ok(f"{name}: scl degrees match the header", got == [float(c) for c in distinct],
       f"{[int(g) for g in got]}")
    ok(f"{name}: scl is ascending", all(b > a for a, b in zip(steps, steps[1:])))

    # ---- the Micro-pitch table ----
    q = OUT / "micropitch" / f"{slug(name)}.txt"
    text = q.read_text()
    listed = dict((m[0], int(m[1])) for m in re.findall(r"^([A-G]#?)\s+([+-]\d+)$",
                                                        text, re.M))
    offs, clashes = micropitch(cents)
    ok(f"{name}: no two degrees want the same key", not clashes, str(clashes))

    # every degree must be reachable: nearest key plus its offset
    rebuilt = set()
    for c in sorted(set(cents)):
        semi = int((c + 50) // 100)
        off = listed.get(NOTE[semi % 12], 0)
        rebuilt.add((semi * 100 + off) % 1200 if semi * 100 + off != 1200 else 1200)
    want = set(c % 1200 for c in set(cents))
    ok(f"{name}: every degree is reachable from the offsets", rebuilt == want,
       f"got {sorted(rebuilt)} want {sorted(want)}")

kbm = (OUT / "tubu.kbm").read_text().splitlines()
nums = [l for l in kbm if l and not l.startswith("!")]
ok("the keyboard map maps seven keys to the octave",
   nums[0] == "7" and nums[6] == "7" and nums[7:] == [str(i) for i in range(7)],
   f"{nums[:8]}")

print(f"\n{'FAILURES' if fails else 'all good'}")
sys.exit(1 if fails else 0)
