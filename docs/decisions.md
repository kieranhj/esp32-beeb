# Decisions

Dated record of the choices that shape the project and why. Add to the end;
do not rewrite history. Verify any file path mentioned still exists before
relying on it.

## 2026-10-11: purpose-built emulator, not a beebjit or b-em port

beebjit's speed comes from an x64/ARM64 JIT. On the ESP32-S3 (Xtensa LX7, no
JIT) only `interp.c` would be usable, and that is beebjit's accuracy-first
slow path; the Master 128 is interpreter-only even on desktop. On the RG40XXV
(Cortex-A53, 1.5 GHz) the Master on the interpreter already ran with "less
margin"; at 240 MHz in-order with small caches it will not reach real time.
`bbc.c` also depends on MMU-aliased memory mappings and a CPU thread with pipe
messaging, both of which would need rewriting.

b-em reached full speed on the Pico (kilograham fork, RP2040 overclocked to
270 MHz; DnCraptor/frank-micro on RP2350 at 252 MHz) only with a hand-written
Thumb 6502 core, and both state cycle accuracy is unfinished. The demos here
depend on exactly the mid-frame CRTC and mid-line palette behaviour that path
gets wrong.

Decision: write a cycle-stepped core for this chip. beebjit stays as the
oracle: its instruction trace (`BEEBJIT_DEBUG=1 BEEBJIT_PRINT=1`) and frame
dump (`BEEBJIT_DUMP_FRAME`) are the truth we diff against. User chose
"accuracy first" when asked.

## 2026-10-11: Master 128 only in v1

The target demos include Master-only productions. A Model B profile is simpler
but would be a second memory model to keep correct; MOS 3.20 runs the great
majority of Model B demos. If a specific disc turns out to need a Model B, add
a profile then, with the OS 1.2 ROM set from `rg40xxv/beebjit/roms/`.

## 2026-10-11: landscape, correct aspect

The panel is 368x448 portrait. The picture is shown landscape at 448x336, a
4:3 frame matching a Beeb monitor, scaled from 640x256 with fixed 10:7 and
16:21 repeat/drop tables. Chosen over crisp 1:1 lines (which would show a
1.75:1 picture) and over a small portrait picture. Rotation is done in the
band writer, not by the panel, because the CO5300 MADCTL rotation path was
not verified on this board.

## 2026-10-11: discs embedded in flash and on SD

Both. The full target set (`discs/`, 134 `.ssd` plus `bs-beeb-niccc.dsd`) is
8.6 MB; the 12 MB `discs` partition holds all of it with the 3 MB app. SD is
for adding discs without reflashing.

## 2026-10-11: mode 7 may slip to v2

Important to the user but allowed to slip if it unblocks the rest. Demos in
`discs/` that need it: `bs-teletextr.ssd`, `mmode7.ssd`, `wip/m7anim.ssd`,
`wip/teletext-slojd.ssd`, and any demo with a mode 7 title or end screen.

## 2026-10-11: out of scope for v1

NuLA (`bs-bbcnula1/2.ssd`, `wip/edge-beeb-nula.ssd`,
`wip/edge-beeb-cpc-nula.ssd`), tape, Tube, serial, ADFS, Music 5000.

## Phase 0 results

(not yet measured)
