#!/usr/bin/env python3
"""Render the differing region of two frame dumps as a PNG, reference above ours.

Usage: framecrop.py reference.bin ours.bin out.png [--whole]

Without --whole the crop covers the bounding box of the differences plus a
margin; with it the whole canvases are stacked.
"""
import struct
import sys
import zlib


def load(path):
    d = open(path, "rb").read()
    w, h = struct.unpack("<II", d[:8])
    return w, h, struct.unpack("<%dI" % (w * h), d[8:8 + w * h * 4])


def png(path, w, h, px):
    raw = b"".join(b"\x00" + bytes(b for p in px[y * w:(y + 1) * w]
                                   for b in ((p >> 16) & 255, (p >> 8) & 255, p & 255))
                   for y in range(h))

    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    whole = "--whole" in sys.argv
    w, h, a = load(args[0])
    _, _, b = load(args[1])
    if whole:
        x0, y0, x1, y1 = 0, 0, w, h
    else:
        ys = [y for y in range(h) if any(a[y * w + x] != b[y * w + x] for x in range(w))]
        if not ys:
            print("identical")
            return 0
        xs = [x for x in range(w) if any(a[y * w + x] != b[y * w + x] for y in ys)]
        y0, y1 = max(0, min(ys) - 24), min(h, max(ys) + 24)
        x0, x1 = max(0, min(xs) - 80), min(w, max(xs) + 80)
        print(f"diff region x {min(xs)}..{max(xs)} y {min(ys)}..{max(ys)}")
    cw, ch = x1 - x0, y1 - y0
    crop = [a[y * w + x] for y in range(y0, y1) for x in range(x0, x1)]
    crop += [b[y * w + x] for y in range(y0, y1) for x in range(x0, x1)]
    png(args[2], cw, ch * 2, crop)
    return 0


if __name__ == "__main__":
    sys.exit(main())
