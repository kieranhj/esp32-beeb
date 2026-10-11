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

## 2026-10-11: Phase 0 results

Measured on the board (ESP32-S3 at 240 MHz, core 1, nothing else running) with
`main/main.c` as committed at this date: a cycle-stepped 6502 in
`main/cpu6502.c`, documented opcodes with the exact NMOS bus sequence, flat
64 KB RAM in internal SRAM. Two workloads of 20 M emulated cycles each: a
synthetic demo inner loop (indirect/indexed copy with zp and abs RMW) and the
first 20 M cycles of Klaus Dormann's functional test (a heavier mix: lots of
compare/branch, stack and flag traffic). Host clocks per emulated 2 MHz cycle:

| Build of the same core                                | synth | klaus | realtime |
| ----------------------------------------------------- | ----- | ----- | -------- |
| First cut: switch dispatch, state through a pointer   | 29.8  | 34.5  | 3.5x     |
| + state in a local copy (no reload after each store)  | 23.7  | 30.2  | 4.0x     |
| + computed-goto table (DRAM), 32-bit cycle counter    | 11.9  | 16.8  | 7.2x     |
| same, switch dispatch instead of the table            | 21.2  | 27.0  | 4.4x     |
| **table build + representative video tick per cycle** | **35.4** | **41.1** | **2.9x** |
| table build, core in flash instead of IRAM            | 11.9  | 16.8  | 7.2x     |
| table + video tick, core (43 KB) in flash             | 35.4  | 41.2  | 2.9x     |
| table build, dispatch table in flash rodata           | 11.9  | 16.8  | 7.2x     |
| same core as a 65C12 (CMOS branches, later same day)  | 12.3  | 17.3  | 6.9x     |
| 65C12 with the video tick                             | 36.3  | 41.1  | 2.9x     |

The video tick (fetch a display byte, expand 8 pixels into a line buffer as
two 32-bit words, advance a horizontal counter, count lines) costs 24 clocks
per cycle on top of the CPU. That is a stand-in for the CRTC/ULA step, not
the real thing, but it is the right shape of work.

**Decision: proceed with the cycle-stepped design, no fallback.** The budget
is 120 clocks per cycle; CPU plus a video-shaped step measures 41 on the worst
workload, leaving about 80 for the real CRTC, ULA, two VIAs and margin.

What the numbers taught, now rules for Phase 1 onwards:

1. **Hot state lives in locals inside the run loop.** A byte store through the
   emulated memory pointer can alias any struct field, so with state behind a
   pointer GCC reloads every register after every bus write. `cpu6502_run`
   copies the state struct to a local and writes it back on exit: 6 clocks
   per cycle. Peripheral state stepped from the per-cycle hook must be in the
   same local copy (Phase 0 used `cpu->hook[]` for this).
2. **Computed-goto dispatch, not `switch`.** GCC 14 for Xtensa compiles a
   dense 256-way switch into a compare tree, not a jump table. The label
   table is worth 9 to 10 clocks per cycle (about 30 per instruction).
3. **32-bit cycle counter in the loop.** The 64-bit increment and compare on
   every bus access showed up; the total is folded into a 64-bit field on
   exit.
4. **IRAM/DRAM placement made no measurable difference** at this size, even
   for the 43 KB video build, because the hot working set fits the 32 KB
   icache and nothing else was running. Keep the placement rule anyway as
   cheap insurance: both cores share the caches, and core 0's panel and
   scaler code will compete in Phase 3. Re-measure then, with core 0 busy.
5. **Flash versus IRAM is not the cache risk; working set is.** Inlining the
   video hook into every bus access grew the core from 10.5 KB to 43 KB. Keep
   the per-cycle step small or out of line, and keep the opcode handlers
   compact, so the hot set stays well under 32 KB once the VIAs and video
   are real.
6. The task watchdog must not watch core 1's idle task
   (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n`); the emulator never yields.

Correctness at this point: Klaus Dormann's 6502 functional test and the NMOS
decimal test pass on the host build and on the board (96,090,063 cycles for
the functional test). Cycle timing of each opcode is by construction, not yet
verified; Phase 1 diffs it against beebjit.

## 2026-10-11: the Master's CPU is a 65C12, and the core follows beebjit's quirks

The first beebjit trace of MOS 3.20 uses TRB and STZ within a dozen
instructions: the Master 128 has a 65SC12, not an NMOS 6502. The plan's
"all 256 opcodes including undocumented ones" was a Model B item. The core
now has a runtime `cmos` flag and two dispatch tables. CMOS: the full 65C02
map minus the Rockwell bit instructions (which the 65SC12 lacks; they are
one-cycle NOPs), read-read-write RMW, 6-cycle shifts on abs,X without a page
crossing, the dummy-read address quirks, decimal mode one cycle longer, JMP
(abs) without the page bug, D cleared on interrupt. The cycle-level bus
sequences follow beebjit's `interp.c` 65C12 paths. NMOS keeps the documented
opcodes only; the undocumented ones wait for a Model B profile.

Tests, all run by `sh host/build.sh` in under two seconds: Klaus Dormann's
6502 functional test (NMOS and CMOS), 65C02 extended opcodes test (the
`65c02.bin` build without Rockwell/WDC ops), NMOS decimal test, 65C02 decimal
test. All pass.

One deliberate difference from beebjit: in decimal mode the 65C02 sets N and
Z from the decimal result (and SBC adjusts the whole byte before the low
nibble, which shows on non-BCD operands); beebjit applies the NMOS flag rules
to the 65C12 as well. The Klaus 65C02 decimal test with every flag check on
is hardware truth, so the core follows it. A trace diff against beebjit can
therefore show N/Z differing right after a decimal ADC or SBC; MOS uses
decimal mode in very few places (the RTC and number printing).

## 2026-10-11: beebjit runs natively on Windows via zig cc

There is no gcc, mingw or WSL on the development machine, but `zig cc` is on
PATH and is a clang. `tools/build_beebjit_win.sh` builds a native beebjit.exe
from `../rg40xxv/beebjit` into `out/beebjit-win/` with one stub for an
assembly label beebjit declares `.globl` but never defines (GNU as lets that
pass, clang's assembler does not). The trace is `-debug -print`, not the
environment variables the plan named; `-master -mode interp -headless -fast`
plus `-commands "breakat N;c;q"` gives N cycles of Master boot in beebjit's
line format:

```
[ITRP] E366: STA $0D00      [A=40 X=00 Y=00 S=FD F=  I  1  ] [addr=0D00 val=FF]
```

The trace diff tool in Phase 1 prints that format from our core.

**`-accurate` is mandatory.** Without it beebjit clocks the VIAs, CRTC and
ADC from wall time even in `-fast` mode, so timer interrupts land at random
emulated cycles and the trace is not reproducible. An afternoon went on a
"spurious" T1 interrupt before that was found. `run_functional_tests.sh`
always passes both flags; so must we.

## 2026-10-11: Phase 1 machine matches beebjit for 1.44 M cycles

`main/machine.c` boots MOS 3.20 on the host and the trace diff against
beebjit (`-master -mode interp -fast -accurate`) is identical for 393,940
instructions including every cycle count, up to the first vsync interrupt,
which needs the real CRTC. Things that had to be right to get there, each
found by the diff:

- power-on register and RAM state as beebjit has them (A=AA, Z set; main
  RAM 0xFF except zero page; Master RAMs zero);
- the 1 MHz stretch on FRED/JIM as well as SHEILA;
- interrupt polling before the penultimate cycle, with CLI/SEI/PLP seen by
  the poll as their old I value and RTI as the pulled one;
- the serial ULA: MOS selects RS423 early, which raises CTS and hides TDRE
  in the ACIA status (0x08, not 0x02);
- the two deliberate differences from beebjit (BIT immediate flags, decimal
  N/Z) are behind `CPU6502_ORACLE_COMPAT`, which only the trace harness
  sets; the real build follows the chip and the Klaus tests.
