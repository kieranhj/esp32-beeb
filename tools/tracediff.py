#!/usr/bin/env python3
"""Compare a beebjit instruction trace with ours and report the first divergence.

Usage: tracediff.py beebjit.txt ours.txt [--context N] [--no-ticks]

beebjit lines (from -debug -print, optionally -opt debug:print-ticks):
  [ITRP] E366: STA $0D00      [A=40 X=00 Y=00 S=FD F=  I  1  ] [ticks=123] ...
  [ITRP] E366: IRQ (IRQ)      [A=.. ...]
ours (host/trace.c):
  E366 40 00 00 FD "  I  1  " 123
  IRQ E366 40 00 00 FD "  I  1  " 123

Registers and flags are compared on every instruction. Ticks are compared as
deltas from the previous instruction, when both traces have them, so a
constant offset between the two counters does not matter.
"""
import re
import sys

BEEBJIT_RE = re.compile(
    r"^\[[A-Z0-9]+\] ([0-9A-F]{4}): (\S+)\s*.*?\[A=([0-9A-F]{2}) X=([0-9A-F]{2}) "
    r"Y=([0-9A-F]{2}) S=([0-9A-F]{2}) F=(.{8})\](?: \[ticks=(\d+)\])?")
OURS_RE = re.compile(
    r"^(?:(IRQ|NMI) )?([0-9A-F]{4}) ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2}) "
    r"([0-9A-F]{2}) (.{8}) (\d+)$")


def parse_beebjit(path):
    out = []
    with open(path, errors="replace") as f:
        for line in f:
            m = BEEBJIT_RE.match(line)
            if not m:
                continue
            pc, op, a, x, y, s, flags, ticks = m.groups()
            kind = "IRQ" if op == "IRQ" else ""
            out.append((kind, pc, a, x, y, s, flags, int(ticks) if ticks else None, line.rstrip()))
    return out


def parse_ours(path):
    out = []
    with open(path, errors="replace") as f:
        for line in f:
            m = OURS_RE.match(line.rstrip("\n"))
            if not m:
                continue
            kind, pc, a, x, y, s, flags, ticks = m.groups()
            kind = "IRQ" if kind in ("IRQ", "NMI") else ""
            out.append((kind, pc, a, x, y, s, flags, int(ticks), line.rstrip()))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    context = 5
    use_ticks = "--no-ticks" not in sys.argv
    for i, a in enumerate(sys.argv):
        if a == "--context":
            context = int(sys.argv[i + 1])
            args.remove(sys.argv[i + 1])
    if len(args) != 2:
        print(__doc__)
        return 2
    ref = parse_beebjit(args[0])
    ours = parse_ours(args[1])
    print(f"beebjit: {len(ref)} instructions, ours: {len(ours)}")
    n = min(len(ref), len(ours))
    prev_ref_t = prev_our_t = None
    for i in range(n):
        r = ref[i]
        o = ours[i]
        bad = []
        if r[0] != o[0]:
            bad.append("kind")
        for name, idx in (("pc", 1), ("a", 2), ("x", 3), ("y", 4), ("s", 5), ("flags", 6)):
            if r[idx] != o[idx]:
                bad.append(name)
        if use_ticks and r[7] is not None and prev_ref_t is not None:
            dr = r[7] - prev_ref_t
            do = o[7] - prev_our_t
            if dr != do:
                bad.append(f"ticks(delta beebjit={dr} ours={do})")
        if bad:
            print(f"DIVERGE at instruction {i}: {', '.join(bad)}")
            lo = max(0, i - context)
            for j in range(lo, min(n, i + 2)):
                mark = ">>" if j == i else "  "
                print(f"{mark} beebjit {j}: {ref[j][8]}")
                print(f"{mark} ours    {j}: {ours[j][8]}")
            return 1
        prev_ref_t = r[7]
        prev_our_t = o[7]
    print(f"identical for {n} instructions")
    if len(ref) != len(ours):
        print("(lengths differ)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
