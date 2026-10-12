#!/usr/bin/env python3
"""Builds the disc bundle image for the `discs` flash partition, and flashes it.

    python tools/make_disc_bundle.py                    # every image in discs/ -> out/discs.bin
    python tools/make_disc_bundle.py --flash COM3       # and write it at the partition offset
    python tools/make_disc_bundle.py a.ssd b.dsd -o x.bin

Format (main/bundle.c reads it through esp_partition_mmap):

    header   "BEEBDISC" magic, u32 count, u32 reserved
    entries  count x { char name[32] (NUL padded), u32 offset, u32 size, u8 sides, u8 pad[3] }
    data     the raw images, each at a 4096-byte aligned offset from the start

Nothing from discs/ is to be published; the bundle stays on the board.
"""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PARTITION_OFFSET = 0x310000   # partitions.csv: after the 3 MB app at 0x10000
PARTITION_SIZE = 12 * 1024 * 1024
ENTRY = struct.Struct("<32sIIB3x")
HEADER = struct.Struct("<8sII")


def align(n):
    return (n + 4095) // 4096 * 4096


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("images", nargs="*", help="disc images (default: every .ssd/.dsd in discs/)")
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "out", "discs.bin"))
    ap.add_argument("--flash", metavar="PORT", help="write the bundle to the board at the partition offset")
    args = ap.parse_args()
    images = args.images or sorted(
        os.path.join(ROOT, "discs", f) for f in os.listdir(os.path.join(ROOT, "discs"))
        if f.lower().endswith((".ssd", ".dsd")))
    entries = []
    blobs = []
    offset = align(HEADER.size + ENTRY.size * len(images))
    for path in images:
        name = os.path.basename(path)
        if len(name.encode()) > 31:
            sys.exit(f"name too long for the directory: {name}")
        with open(path, "rb") as f:
            data = f.read()
        sides = 2 if name.lower().endswith(".dsd") else 1
        entries.append(ENTRY.pack(name.encode(), offset, len(data), sides))
        blobs.append((offset, data))
        offset = align(offset + len(data))
    if offset > PARTITION_SIZE:
        sys.exit(f"bundle is {offset} bytes, the partition {PARTITION_SIZE}")
    image = bytearray(offset)
    image[:HEADER.size] = HEADER.pack(b"BEEBDISC", len(images), 0)
    pos = HEADER.size
    for e in entries:
        image[pos:pos + ENTRY.size] = e
        pos += ENTRY.size
    for off, data in blobs:
        image[off:off + len(data)] = data
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(image)
    print(f"{len(images)} discs, {offset} bytes -> {args.out}")
    if args.flash:
        cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", args.flash, "-b", "921600",
               "write_flash", hex(PARTITION_OFFSET), args.out]
        print(" ".join(cmd))
        sys.exit(subprocess.call(cmd))


if __name__ == "__main__":
    main()
