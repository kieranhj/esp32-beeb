#!/usr/bin/env python3
"""Compare two frame dumps (beebjit dumpframe format) and report differences.

Usage: framediff.py reference.bin ours.bin [--ppm prefix]

Prints the number of differing pixels, the first difference, and the rows
that differ. With --ppm it writes prefix_ref.ppm, prefix_ours.ppm and
prefix_diff.ppm (differences in red on a dimmed reference).
"""
import struct
import sys


def load(path):
    d = open(path, "rb").read()
    w, h = struct.unpack("<II", d[:8])
    px = struct.unpack("<%dI" % (w * h), d[8:8 + w * h * 4])
    return w, h, px


def write_ppm(path, w, h, px):
    with open(path, "wb") as f:
        f.write(b"P6 %d %d 255\n" % (w, h))
        f.write(bytes(b for p in px for b in ((p >> 16) & 255, (p >> 8) & 255, p & 255)))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    prefix = None
    if "--ppm" in sys.argv:
        prefix = sys.argv[sys.argv.index("--ppm") + 1]
        args.remove(prefix)
    if len(args) != 2:
        print(__doc__)
        return 2
    w1, h1, a = load(args[0])
    w2, h2, b = load(args[1])
    if (w1, h1) != (w2, h2):
        print(f"size mismatch: {w1}x{h1} vs {w2}x{h2}")
        return 1
    w, h = w1, h1
    diff_rows = {}
    first = None
    count = 0
    for i, (p, q) in enumerate(zip(a, b)):
        if p != q:
            count += 1
            y, x = divmod(i, w)
            if first is None:
                first = (x, y, p, q)
            diff_rows[y] = diff_rows.get(y, 0) + 1
    print(f"{w}x{h}: {count} differing pixels")
    if first:
        x, y, p, q = first
        print(f"first at x={x} y={y}: reference {p:08x} ours {q:08x}")
        rows = sorted(diff_rows)
        print(f"rows differing: {len(rows)} (first {rows[:10]}{'...' if len(rows) > 10 else ''})")
    if prefix:
        write_ppm(prefix + "_ref.ppm", w, h, a)
        write_ppm(prefix + "_ours.ppm", w, h, b)
        d = [((p >> 1) & 0x7f7f7f) if p == q else 0xff0000 for p, q in zip(a, b)]
        write_ppm(prefix + "_diff.ppm", w, h, d)
    return 0 if count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
