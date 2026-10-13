#!/usr/bin/env python3
"""Sends a console command to the board and prints what comes back.

    python tools/beeb.py COM3 list
    python tools/beeb.py COM3 boot bs-twisted.ssd      # persists the choice and resets
    python tools/beeb.py COM3 reset
    python tools/beeb.py COM3 menu                     # the disc menu on the panel

The board answers on the same port; `boot` and `reset` re-enumerate it, so
the port is reopened and the boot log captured for a few seconds.
"""
import sys
import time

import serial


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    port = sys.argv[1]
    line = " ".join(sys.argv[2:])
    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.reset_input_buffer()
        s.write((line + "\n").encode())
        s.flush()
        end = time.time() + 2.0
        while time.time() < end:
            try:
                data = s.read(4096)
            except serial.SerialException:
                print()
                print("(port closed: the board reset or was unplugged)")
                break
            if data:
                sys.stdout.write(data.decode(errors="replace"))
                sys.stdout.flush()
    if line.split()[0] in ("boot", "reset", "menu"):
        end = time.time() + 12.0
        while time.time() < end:
            try:
                with serial.Serial(port, 115200, timeout=0.2) as s:
                    while time.time() < end:
                        data = s.read(4096)
                        if data:
                            sys.stdout.write(data.decode(errors="replace"))
                            sys.stdout.flush()
            except serial.SerialException:
                time.sleep(0.3)


if __name__ == "__main__":
    main()
