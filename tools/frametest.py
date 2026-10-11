#!/usr/bin/env python3
"""Frame diffs against beebjit for the MOS screen modes and simple graphics.

Each test types a line at the BASIC prompt through the keyboard matrix on
both emulators (beebjit: keydown/keyup debugger commands, applied by its
2 ms wakeup timer; ours: --key events at the same cycles, chosen on the
wakeup grid so both press at the same moment), runs to the end cycle and
compares the two canvases with tools/framediff.py.

Usage: frametest.py [test-name ...]       (from the repo root)
Needs out/beebjit-win/beebjit.exe (tools/build_beebjit_win.sh) and
host/frame.exe.
"""
import os
import subprocess
import sys

BJ = os.path.join("out", "beebjit-win", "beebjit.exe")
FRAME = os.environ.get("FRAME_EXE", os.path.join("host", "frame.exe"))
OUT = os.path.join("out", "frametest")

# BBC keyboard matrix (row, col) and the beebjit key code, from beebjit's
# keyboard_bbc_key_to_rowcol. Shifted characters list the unshifted key.
KEYS = {
    "A": (4, 1, "A"), "B": (6, 4, "B"), "C": (5, 2, "C"), "D": (3, 2, "D"),
    "E": (2, 2, "E"), "F": (4, 3, "F"), "G": (5, 3, "G"), "H": (5, 4, "H"),
    "I": (2, 5, "I"), "J": (4, 5, "J"), "K": (4, 6, "K"), "L": (5, 6, "L"),
    "M": (6, 5, "M"), "N": (5, 5, "N"), "O": (3, 6, "O"), "P": (3, 7, "P"),
    "Q": (1, 0, "Q"), "R": (3, 3, "R"), "S": (5, 1, "S"), "T": (2, 3, "T"),
    "U": (3, 5, "U"), "V": (6, 3, "V"), "W": (2, 1, "W"), "X": (4, 2, "X"),
    "Y": (4, 4, "Y"), "Z": (6, 1, "Z"),
    "0": (2, 7, "0"), "1": (3, 0, "1"), "2": (3, 1, "2"), "3": (1, 1, "3"),
    "4": (1, 2, "4"), "5": (1, 3, "5"), "6": (3, 4, "6"), "7": (2, 4, "7"),
    "8": (1, 5, "8"), "9": (2, 6, "9"),
    " ": (6, 2, " "), ",": (6, 6, ","), ".": (6, 7, "."), "/": (6, 8, "/"),
    ";": (5, 7, ";"), ":": (4, 8, "'"), "-": (1, 7, "-"),
    "\n": (4, 9, "131"),
}
SHIFTED = {"%": "5", "*": ":", "+": ";", "=": "-", "(": "8", ")": "9", "<": ",",
           ">": ".", "!": "1", '"': "2", "&": "6", "'": "7", "?": "/"}
SHIFT = (0, 0, "133")   # left shift


def key_events(text, start, step=160000, hold=80000):
    """Yields (cycle, row, col, down, beebjit_code). Cycles are multiples of
    20000: beebjit applies a queued key at the next multiple of 20000 after
    the breakpoint, so breaking 2000 cycles early makes both sides press at
    the same cycle. The step and hold keep every event on that grid."""
    t = start
    for ch in text.upper() if text != "\n" else text:
        pass
    events = []
    for ch in text:
        c = ch.upper() if ch != "\n" else ch
        shifted = c in SHIFTED
        base = SHIFTED[c] if shifted else c
        row, col, code = KEYS[base]
        if len(code) == 1:
            code = str(ord(code))  # beebjit key codes are ASCII for the printable keys
        if shifted:
            events.append((t - 20000, SHIFT[0], SHIFT[1], 1, SHIFT[2]))
        events.append((t, row, col, 1, code))
        events.append((t + hold, row, col, 0, code))
        if shifted:
            events.append((t + hold + 20000, SHIFT[0], SHIFT[1], 0, SHIFT[2]))
        t += step
    return events


def run_test(name, text, start=1520000, trace=False):
    os.makedirs(OUT, exist_ok=True)
    events = key_events(text + "\n", start)
    end = events[-1][0] + 1500000
    bj_cmds = []
    for cycle, row, col, down, code in events:
        # The key lands at the next wakeup after the breakpoint; break 2000
        # cycles early so that is the intended cycle.
        bj_cmds.append(f"breakat {cycle - 2000};c;{'keydown' if down else 'keyup'} {code}")
    bj_bin = os.path.join("..", "frametest", f"{name}_bj.bin")
    bj_cmds.append(f"breakat {end};c;dumpframe {bj_bin};q")
    bj_args = [os.path.abspath(BJ), "-master", "-mode", "interp", "-headless", "-fast",
               "-accurate", "-debug", "-frame-cycles", "1", "-opt", "video:always-render"]
    if trace:
        bj_args += ["-print", "-opt", "debug:print-ticks"]
    bj_args += ["-commands", ";".join(bj_cmds)]
    with open(os.path.join(OUT, f"{name}_bj.txt"), "w") as bj_out:
        subprocess.run(bj_args, cwd=os.path.dirname(BJ), stdout=bj_out if trace else subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    ours_bin = os.path.join(OUT, f"{name}_ours.bin")
    args = [FRAME, os.path.join("roms", "mos3.20"), str(end), ours_bin]
    if trace:
        args += ["--trace", os.path.join(OUT, f"{name}_ours.txt")]
    for cycle, row, col, down, code in events:
        args += ["--key", str(cycle), str(row), str(col), str(down)]
    subprocess.run(args, stderr=subprocess.DEVNULL)
    r = subprocess.run([sys.executable, "-I", os.path.join("tools", "framediff.py"),
                        os.path.join(OUT, f"{name}_bj.bin"), ours_bin, "--ppm",
                        os.path.join(OUT, name)], capture_output=True, text=True)
    print(f"{name}: {r.stdout.strip().splitlines()[0] if r.stdout.strip() else r.stderr.strip()}")
    return r.returncode == 0


TESTS = {
    "mode0": "MODE 0",
    "mode1": "MODE 1",
    "mode2": "MODE 2",
    "mode3": "MODE 3",
    "mode4": "MODE 4",
    "mode5": "MODE 5",
    "mode6": "MODE 6",
    "lines2": "MODE 2:FOR I=0 TO 15:GCOL 0,I:MOVE I*80,0:DRAW I*80+79,1023:NEXT",
    "vdu19": "MODE 1:VDU 19,0,4,0,0,0,19,3,10,0,0,0:PRINT 12345",
}


def main():
    trace = "--trace" in sys.argv
    names = [a for a in sys.argv[1:] if not a.startswith("--")] or list(TESTS)
    ok = True
    for name in names:
        if not run_test(name, TESTS[name], trace=trace):
            ok = False
        if trace:
            subprocess.run([sys.executable, "-I", os.path.join("tools", "tracediff.py"),
                            os.path.join(OUT, f"{name}_bj.txt"), os.path.join(OUT, f"{name}_ours.txt")])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
