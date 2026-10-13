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

## 2026-10-11: CRTC in, 10 M cycles of boot identical to beebjit

`main/crtc.c` is beebjit's `video_advance_crtc_timing` turned into a
per-tick stepper with the same latches in the same order (the start-of-line
check chain, R4/R5/R9 hits latched per scanline, odd and even vsync in
interlace, the dummy raster, R7 writes starting a vsync at once). With it the
trace diff is identical for the first 10 M cycles (2,768,856 instructions),
which covers boot, the BASIC prompt, cursor flashing and the MOS's periodic
ADC conversions. Phase rules that fell out, all forced by the diff:

- The CRTC processes system tick k during the CPU cycle that ends at count
  k+1, in 1 MHz mode stepping its counters on odd ticks and fetching on even
  ones.
- The VIAs step on odd cycle counts, not on the even 1 MHz boundary: beebjit
  expires T1 on the tick after the boundary, and a 1 MHz access lands on an
  odd count with that step already applied. A T1 write loads latch + 1 so
  that the readback and the fire time both match.
- The ADC is a 2 MHz-cycle countdown from the end of the stretched write
  (it can expire on an even count), 20000 or 8000 cycles.
- One cycle of phase on any of these moves an interrupt across an
  instruction boundary, which the diff catches because the IRQ line is
  sampled before the penultimate cycle.

## 2026-10-12: the machine on the board is 8x too slow stepped per cycle

First run of the whole machine (CPU, VIAs, CRTC counters, ADC) on the
ESP32-S3, MOS 3.20 at the BASIC prompt, measured with profile switches that
turn parts off:

| Configuration                                   | clocks/cycle |
| ----------------------------------------------- | ------------ |
| full machine                                    | 206          |
| no CRTC step                                    | 123          |
| no CRTC, no VIA step (empty per-cycle loop)     | 88           |
| catch-up call per instruction only, empty       | 40           |
| core alone with page tables and I/O slow paths  | 26           |

Three lessons, each costing an experiment:

1. **One call per emulated cycle is unaffordable**: the Xtensa windowed ABI
   keeps only a2-a7 of the caller live across a call8, so the core's state
   spills around every call. Measured at 60+ clocks per cycle. A first
   restructure moved to one call per instruction (the beebjit shape: the
   core counts cycles, the machine catches up), which is 14 clocks per
   cycle.
2. **Even an empty per-cycle loop in the catch-up costs about 49 clocks per
   cycle** (taken branches are 2 to 3 clocks each on the LX7 and there is no
   branch prediction). Peripherals must advance in bulk between events: VIA
   timers by subtraction with the next underflow precomputed, the ADC by a
   countdown, the CRTC by a per-character loop with a fast path for the
   middle of a scanline.
3. **ROM in flash is free**: copying the MOS into internal RAM changed
   nothing (identical timings to the microsecond), so the 64 KB data cache
   holds the hot ROM. ROMs stay in flash as planned.

The core itself went from 12 (Phase 0, flat memory) to 26 with page tables,
the sync branch on writes and the MOS's hardware polling through the slow
path; acceptable, to be revisited after the peripherals are fixed.

## 2026-10-12: event-driven catch-up brings the machine to 95 clocks per cycle

Same measurement after the restructure, MOS at the BASIC prompt:

| Configuration                          | clocks/cycle |
| -------------------------------------- | ------------ |
| full machine                           | 95           |
| no CRTC                                | 58           |
| no CRTC, no VIAs                       | 50           |
| core alone                             | 32           |

How it is now shaped (`machine_advance` in `machine.c`):

- The core only counts cycles. It catches up at an instruction boundary
  only when `sync_due` has arrived: the earliest VIA timer underflow or
  reload, ADC completion, or the next CRTC point where vsync could change
  (end of line, or half-R0 in a vsync row), capped at 4096 cycles. Between
  catch-ups the IRQ history is the last level repeated.
- VIA timers advance by subtraction with the next event precomputed
  (`via_ticks_to_event`); the exact per-tick code runs only on the event
  tick. T1 now reloads from the latch on every underflow in both modes,
  which is what beebjit's readback does.
- The CRTC runs a per-tick loop, but with a fast path (`crtc_fast_ticks`)
  that computes how many ticks can pass before any register hit, cursor
  match, hsync, start-of-line chain or vsync point, and runs those with
  only fetch, render hook and address increment. 82% of ticks take it at
  the mode 7 prompt; the rest go through the exact tick.
- RAM writes catch up only when the address falls in the window of display
  memory the CRTC will fetch before `sync_due` (`wlo..whi`, published by
  the machine). That took write catch-ups from 3.2 M to 75 k per 20 M
  cycles; the MOS idle loop writes its workspace constantly.
- Hardware accesses still cost a call each; the MOS idle loop makes one
  every 13 cycles polling the keyboard. Demos do far less of that.

Where the remaining 95 go, roughly: core 32, catch-up calls 10, VIAs 8,
CRTC slow ticks 18 (hsync, line start chain, register hits), CRTC fast
ticks 12, chunk bookkeeping 7, hardware access slow paths 8. The pixel
side will be a byte stream per scanline from the fast path with the ULA
writes logged against their horizontal position; pixels get made from
that on core 0, so core 1's video cost stays near the fetch.

## 2026-10-12: video as a stream, rendered beebjit's way, pixel identical

The CRTC side emits one 16-bit word per 2 MHz tick (data byte, DISPEN
after skew, odd half, MA13 in the flags byte, bit 7 clear) and tagged
events (bit 7 set: hsync width, vsync level, RA/ISV, cursor, ULA control,
ULA palette, framing change) into a ring (`main/vstream.h`). The renderer
(`main/render.c`) is a port of beebjit's render.c beam model: 8 pixels per
tick, hsync to the left and down two rasters, vsync to the top, two rasters
per scanline, the forced hsync at 1536 pixels and the forced vsync at 768
rasters, cursor XOR segments, mode tables rebuilt from the ULA palette.
Teletext (`main/teletext.c`) is a port of beebjit's SAA5050 with b-em's
glyphs. Ten frame comparisons against beebjit's patched `dumpframe` are
pixel identical. Things the diff taught:

- The stream encoding must not use bit 15 for events: data bytes with bit 7
  set (any bitmap mode) became events and vanished. Bit 7 of the flags byte
  is the discriminator now.
- beebjit clears its whole render buffer at the flyback that follows a
  framing change (CRTC R0-R9 write, ULA clock or teletext change). That is
  a display policy, not hardware; the host renderer copies it behind
  `compat_clear` so transitional frames compare. The board will not do it.
- beebjit re-evaluates its line-start latches at the start of every video
  advance, which follows any register write within a few cycles; the CRTC
  now does that at the write. It made no visible difference in these tests
  but keeps the models aligned for register writes mid-line.
- Keyboard: with a key held in auto-scan mode, the CA2 flag comes straight
  back when the MOS clears it (the scan passes the key again); beebjit
  models this in its IFR write and now so does the VIA via its port
  callback. Key presses for tests are scheduled inside the machine at an
  exact cycle and applied after that cycle's IRQ level is recorded, which
  is where beebjit's queued keys land relative to the poll.
- beebjit applies a queued key at the next multiple of 20000 cycles after
  the breakpoint that queued it; `tools/frametest.py` breaks 2000 cycles
  early and schedules ours on that grid so both sides press at the same
  cycle. Its 2 ms wakeup is 100 Hz in fast mode.
- beebjit's debugger `eval` of VIA registers is stale (timers sync lazily),
  but `crtc_c0/c4/c9/ma` and `render_x/y` are advanced on read and were
  the tools that found the last differences.

Licensing: beebjit is GPLv3 and `teletext_glyphs.h` is Sarah Walker's GPL
data. The renderer and teletext code are ports and the CRTC, VIA and 65C12
quirks are restated from beebjit, so if this emulator is ever published it
is GPLv3, or those parts get a clean-room rewrite. Nothing here can be
published anyway while it ships the ROMs and discs.

## 2026-10-12: Phase 3, the picture on the panel

Shape: the CRTC stream crosses to core 0 through a 32 KB ring; a render
task there runs the beebjit-model renderer with 3-bit pixels (`pixel.h`,
`RENDER_INDEXED`) into two raster line buffers and, at each hsync, scales
the raster into the current band buffer (16 panel rows, 14 KB, two of them
in DMA-capable SRAM). The panel is rotated by its own MADCTL, so a band is
a run of landscape rows and a frame is 23 queued bands. There is no frame
buffer anywhere; the panel trails the beam by a band.

Numbers (240 MHz host clocks; a stream word is one 2 MHz tick, or one
character in 1 MHz modes):

- Core 1, the machine: 96 clocks per cycle at the BASIC prompt with no
  stream, 106 with it (the stream store is batched per CRTC run), 118 to
  128 while a BASIC program draws lines in mode 2. The latter is over
  budget: each screen write inside the display window syncs the CRTC.
- Core 0, the renderer alone, synthetic streams: 72 clocks per word in
  mode 2 (64 words a line take the fast path, 2 the slow), 186 per
  character word in mode 7, 170 per word down the slow path.
- Core 0, the panel path: 2.2 K clocks to scale a 448 pixel row (4.9 per
  pixel), 37 us of SPI driver time to queue a band, 400 us for the band
  to go out at 80 MHz quad (the link is busy 46% of a frame).
- Core 0 in real time: 80 to 84% busy with the mode 2 line drawer, 99 to
  100% at the mode 7 prompt (84% rendering, 16% panel), where it drops
  frames. The ring laps the renderer when it is behind and the renderer
  resyncs to the newest half ring.

What it took to get there, each one measured on the board:

- Internal RAM is the constraint, not flash or speed. The first link
  overflowed by 140 KB. The fixes: teletext glyph sets as one bit per
  pixel (11 KB, not 92), one expansion table per clock rather than per
  mode (6 KB, not 24), the stream ring halved, IDF's FreeRTOS, heap, ring
  buffer and flash code moved out of IRAM (`sdkconfig.defaults`). 90% of
  the 334 KB IRAM+DRAM segment is used; 72 KB of heap is left at run time.
- The renderer's per-word path costs about 170 clocks on this core (the
  windowed ABI, struct traffic, load-use stalls), too slow for 2 M words a
  second. Runs of plain ticks inside the window, and outside it, go
  through loops with everything hoisted (`render_consume_n`); the frame
  tests run through the same loops on the host, so they stay beebjit
  identical.
- The CRTC emits one word per character in 1 MHz modes (`VS_PAIR`): halves
  the stream for modes 4 to 7 and the core 1 cost of it; the renderer
  treats a pair as the even tick then the odd one, and the pixel results
  are identical.
- The teletext pipeline packs into two words; in bitmap modes it is at a
  fixed point after two calls and the calls are skipped (provably the same
  state); the decode of bytes is skipped unless teletext is being
  rendered, which beebjit does not do, and which only differs in the
  scanline a mode change to teletext lands on.
- The second raster of a scanline is only rendered for the 5 in 16
  scanlines the panel doubles (`RENDER_LINE2_WANTED`); the canvas on the
  host always wants it.
- esp_lcd's SPI panel io cannot queue a band while one is in flight (it
  polls every command, draining the queue first): the measured band flush
  waited a whole transfer. `display.c` talks to the SPI master driver
  directly: a register write is opcode 0x02 with the register in the
  24-bit address, pixels are 0x32 00 2C 00 then four-line data, both as
  queued transactions, the column window set once at init.
- Code placement: the renderer's per-tick path in IRAM made no measurable
  difference (it was never instruction fetch); it stays there because the
  budget is tight enough that a cache miss on a hot line would show.

Still owed: a person looking at the panel. The mirror bits and which axis
the V2's 16 pixel gap lands on after rotation are guesses until then.

## 2026-10-12: the 1770 and discs, cycle identical to beebjit

The disc side is beebjit's model restated: a disc is pulses (32 two
microsecond slots per 64 us position, 3125 positions a track, built from the
.ssd with the standard DFS gaps), the drive delivers one position per timer
tick, the controller recovers marks and bytes from the pulses and runs the
command state machine on every byte, with its own timers for spin-up, seek
steps, settle and the 32 us completion. Anything less would not have given
the same NMI cycles, and the NMI cycles are what the trace diff checks: an
autobooted CRTC test disc is identical for 6 M cycles (2.01 M instructions,
2061 NMIs, every drive tick on the same cycle). What it took, each found by
the diff:

- The core runs on a local copy of its state, so the machine cannot set
  its NMI flag directly: the edge and its cycle go through the catch-up
  hooks like the IRQ line (`machine.nmi_edge`).
- The interrupt poll is two cycles before the instruction's end *not
  counting a 1 MHz access's stretch*: beebjit's interpreter polls before
  the access, so an edge during the stretch waits an instruction. An IRQ
  being taken vectors through NMI if the edge arrived after the poll.
- Timers started from a 1770 register write (FE28-FE2B, 1 MHz) fire one
  cycle later than ones started from a callback, and ones started from the
  control register write (FE24, 2 MHz) do not: beebjit does the 1 MHz write
  at the end of the stretched access, our write cycle is its start.
- Same-cycle timers fire in arming order (beebjit's expiry list puts an
  equal expiry after the ones already there). It did not matter in the
  end but it is what the model does.
- The keyboard's row 0 (shift, ctrl) is not wired to the CA2 interrupt:
  holding shift at boot must not raise it.
- beebjit's `-autoboot` is shift held from power on and released by a
  timer at cycle 3000000; `machine_autoboot()` does the same.
- Floating point: the drive's position-to-tick arithmetic is beebjit's
  double sequence step for step, because its truncation decides whether a
  position takes 127, 128 or 129 cycles.

Frame diffs on the five CRTC test discs then found two things. The display
was reading LYNNE whenever ACCCON X was set with D clear: beebjit's test is
D ^ X only because its "main" pointer is the CPU's current view, which X
swaps; the hardware rule is D alone. And `crtc-somenastyeffects` moves the
picture with an R7 write mid-frame, after which beebjit's canvas keeps a
few cells of the old frame where ours had cleared: a paint scheduling
detail, not hardware, so the frame tests now run both sides with the
canvas cleared at every flyback (`video:always-clear`, `FRAME_ALWAYS_CLEAR`).

Open: a byte the CPU writes on the very cycle the CRTC fetches it. One
MODE 2 pixel differed in a frame dumped mid-DRAW; the `lines2` case now
settles first. Whether beebjit fetches before or after the write on that
cycle is still to be read out of its video memory sync.

Board: the disc code is kept out of IRAM (`FDC_API`, `DISC_API` noinline:
one call per 64 us while a disc spins) and the pulses will live in PSRAM,
1 MB a side. Also found here: the core's instruction loop head is
duplicated into every opcode's tail by the compiler, so a few lines added
there cost 34 KB of IRAM; the interrupt decision is now out of line.

## 2026-10-12: a demo on the board, profiled: core 1 148 -> 106, core 0 99 -> 80

The first disc on the board (`crtc-tmoyos.ssd` embedded in the image and
autobooted) ran at 148 clocks per cycle on core 1, core 0 saturated, the
lag growing. Both were brought inside real time by measuring, not by
guessing; the tools are now part of the repo.

Measuring: a profile build (`-DBEEB_PROFILE`, see CLAUDE.md) compiles the
machine's counters with the Xtensa cycle counter around each part of the
catch-up and prints a PROF line every 5 s; the host bench
(`host/bench.exe --disc --autoboot --from`) gives the same counters for a
window of a demo in a second. The two disagree on proportions (the board's
in-order core pays five times the host's clocks for the same code) so the
board's numbers decide and the host's find the counts behind them. The
renderer reports its event counts, table rebuild time and the panel path
split in the VIDEO and PANEL lines.

Core 1, in the order the profile pointed at them:

- The CRTC fast path cost 48 clocks a tick: it recomputed the display
  address and branched on every tick. It now asks the machine once per
  run for a pointer, a stride and how many characters stay linear
  (`machine_display_run`: to the next MA wrap boundary or RAM region)
  and streams bytes with a load and a store each. 20 clocks a tick.
- Exact ticks cost 170 to 240 clocks and were 13% of all ticks. An hsync
  in progress is only a countdown, the start-of-line chain only shifts
  latches whose inputs a register write would resync first, and the
  DISPEN pipeline settles over three ticks that are each a shift: all
  three now run inside the fast path. Exact ticks are the counter hits
  alone, 2.4%.
- Every 1 MHz access made three catch-up calls (to the access, each idle
  cycle, the cycle after); it is two, the idle cycle or two merged with
  the catch-up and the scheduling deferred to the last.
- The scheduler made the core sync every cycle for the four ticks of the
  line-start chain, 31000 syncs a second, for nothing the chain can
  change. Removing them exposed a hole the syncs had covered: a key event
  applies after its cycle's IRQ level is recorded, and the core carries
  the old level until it next syncs, so the keyboard interrupt arrived
  late unless something synced within a few cycles. Every test key lands
  on a line start. The machine now flags a key event that moved the
  level and brings the core back one cycle later; `vdu19` was three
  scanlines of palette wrong without it.
- The demo rewrites ROMSEL/ACCCON 13000 times a second; a write that
  changes nothing no longer remaps. The 1770's event query is inline in
  IRAM (it ran from flash once per chunk). The sync hook's history length
  is clamped to 16: a window of 32 or more cycles shifted by its length
  modulo 32 and kept stale bits.

Core 0:

- The demo writes the palette per scanline, and every write rebuilt the
  whole expansion table: 74% of core 0. Three steps: rebuild only the
  entries whose pixels use the changed colour, lazily once per scanline
  (22%); then no rebuild at all. On the board a pixel is now a colour
  code (`pixel.h`): a logical colour the emitter resolves through the
  palette when the span is sent to the scaler, or a physical one for
  teletext and blanking, plus the cursor's inversion bit. A palette
  write or flash change ends the span and rebuilds a 64-entry RGB565
  lookup. The tables depend on the mode alone. The host keeps beebjit's
  ARGB pixels as the reference and its indexed build resolves the same
  way, so both are checked against beebjit.
- The run loop's bitmap case is four loops (clock x row doubling) with
  the checks a run cannot change hoisted out: 74 -> 59 clocks a word.
- The machine ran 2 ms ahead in slices and, when behind, 40000 cycles at
  once: it lapped the ring and the picture tore. It now waits when fewer
  than 2000 words are free; a saturated core 0 slows the machine instead.
  A third band buffer (from the DMA-capable heap, the static image being
  full) cut the wait for the DMA from 9% to 5%.

Where it stands on this demo, paced: core 1 at 106 clocks per cycle (the
core about 45, CRTC 41, catch-up overhead 20); core 0 at 80% (bitmap
words 59 clocks each, the scaler 14%, queueing bands 7%, waiting for the
DMA 5%, events 7%). Mode 7 is unchanged at the limit: 186 clocks a
character word in the renderer.

## 2026-10-12: seven demos on the board, and what an I/O access costs

With the embedded disc selectable (`idf.py "-DBEEB_DISC=x.ssd" build`)
seven bitshifters discs were run for 75 s each. Core 1 in clocks per
cycle (budget 120) and core 0's load, after the changes below:

| disc | core 1 | core 0 | what it is doing |
|---|---|---|---|
| crtc-tmoyos | 102 | 65-77% | palette and CRTC writes per scanline |
| bs-wave-runner | 92 | 70% | real time |
| bs-patarty | 156 | 52% | palette per scanline (before the changes below) |
| bs-twisted | 180 | 53% | a CRTC register, the palette and a VIA every scanline |
| bs-sotc | 139 | 55% | polls the system VIA for vsync every 8 cycles |
| bs-mott | 141 | 54% | the same |
| bs-bar-beeb, bs-beebstep | 74, 115 | 99% | mode 7 screens: core 0 is the limit |

Each one taught something, all checked by the trace diff (6 M cycles
with a disc, 2 M without) and the 16 frame cases, identical throughout:

- The cycle after a register access was a catch-up call of its own. Now
  the core counts it as stretch and the machine only redoes the
  scheduling, unless the access moved the IRQ level or raised an NMI
  edge, when that cycle is advanced and recorded as before. Two calls per
  access at most. Twisted Brain 221 -> 181.
- A scanline with several spans (one per palette write) whose doubled
  panel row fell in the next band moved to that band mid-scanline, then
  sought back for the next span with a fresh buffer: 20 extra bands a
  frame, rows lost, a third of core 0 waiting for the DMA. The doubled
  row now waits for the hsync. 0 backwards seeks.
- An access to anything but the CRTC, the ULA or paging leaves the CRTC
  behind (`crtc_synced`), provided the scheduler's own bound says no vsync
  change can fall in the window, so the IRQ history recorded without it
  holds; the CRTC catches up in one run at the next full catch-up, where
  its fast path gets whole lines. A counter checks the bound: zero
  violations on every disc. sotc 208 -> 182.
- sotc and mott sit in a two-instruction loop reading the system VIA's
  IFR, 116000 times a million cycles, waiting for vsync. A VIA access
  with nothing else pending (no VIA event, ADC, key, 1770 or possible
  vsync change in the window) now steps the VIAs by subtraction, records
  the held level, does the access and reschedules from the VIA's next
  event against the sync point the write window was computed for.
  Catch-up calls on sotc 123000 -> 10000 a million cycles; 182 -> 139.
- That build crossed the IRAM segment's jump range (`j: cannot encode:
  xt_debugexception` at about 168 KB of IRAM text; the core alone is
  100 KB). The renderer now runs from flash through the instruction
  cache, which measured the same (MODE 7 bench 228 -> 229 clocks a word,
  tmoyos' core 0 no worse), and 48 KB of the segment is free.

What is left on sotc is the core: `LDA $FE4D` / `BPL` at 8 cycles a
pair costs about 100 clocks a cycle before the machine does anything,
200 clocks an instruction, against about 45 a cycle on ordinary code.
The I/O instruction's path through the core (the call out, the result
applied to the core's state) has not been looked at since Phase 1 and
is the next thing to measure. The core 0 limit is mode 7: 229 clocks a
character word with the panel path, and a full screen of it is a
million words a second.

## 2026-10-12: sound, asynchronously for now

The SN76489 is beebjit's model restated (`main/sn76489.c`): registers,
the tone counters stepping at 250 kHz and underflowing from 0 to 0x3ff,
the noise register updated every second expiry with half periods, the
power-on state (full volume, lowest tone, noise frequency 2), 2 dB a
volume step with silence at zero. Its output is unipolar, so a one-pole
DC blocker sits before the speaker.

What is not beebjit's is the write timing. beebjit's synchronous mode
models the chip's 8 us write cycle against the 1 MHz bus (a transparent
address latch open for 2 us, the register latch for the next 2 us, a
byte missed if the write enable meets a positive 250 kHz edge exactly).
Here a byte is taken when the system VIA's IC32 bit 0 goes low with it
on port A, or when port A changes while it is low: the MOS and most
music players are fine with that, a sample player hammering the volume
register may not be. The hook is in the machine (`MACHINE_SOUND_WRITE`),
the platform renders.

On the board the bytes go to a lock-free queue with their cycle, and a
task on core 0 renders the chip 4 ms behind the machine's cycle count,
8192 cycles at a time, applying the queued bytes at their cycles: 128
samples at 31250 Hz (eight 250 kHz steps averaged: the chip's step rate
divided by eight, so no resampling), written to the ES8311 through
esp_codec_dev over I2S (pins from the Waveshare BSP: MCLK 16, BCLK 9,
WS 45, data 8, amplifier enable 46); the codec write blocks when the
DMA buffers are full and that paces the task. When the machine falls
behind real time the audio waits with it. The host harness
(`host/sound.exe`) writes the same samples to a WAV: the boot beep
(27 bytes, 523 Hz for 0.28 s after the power-on blast) and patarty's
tune come out as expected. Nobody has listened on the board yet; the
codec opens and the samples flow.

## 2026-10-12: input, and two boot failures it exposed

Input is the least that plays a demo: touch holds Space, a long press
adds Return, a swipe changes disc (`main/input.c`). The CST820 is read
directly over I2C (register 0x00: fingers in byte 2, x and y in bytes 3
to 6), the PWR button through the TCA9554 expander's EXIO4 as FluidBox
does. Keys reach the machine through the same cycle-stamped queue the
frame tests use, 1 ms ahead of the machine; the queue now restarts when
drained so live input never runs out of slots.

Two failures appeared with it, neither caused by it:

- esp_partition_mmap of the disc bundle returned data-bus addresses
  inside the range the PSRAM heap occupies (0x3c810000 with the pool
  running to 0x3c890000 on one build, just past it on another). The
  flash pages replaced PSRAM pages under the heap and the next heap walk
  faulted. It only showed once an NVS read walked the heap (after the
  console stored a volume). The bundle is now read, not mapped: the
  directory into internal RAM, an image into PSRAM while its pulses are
  built.
- The machine task's 16 KB stack no longer fitted in the internal heap
  (24 KB free, the largest block 15 KB, after the third band buffer,
  the codec and three more tasks) and xTaskCreatePinnedToCore failed
  silently. Its stack and TCB are static now; 18 KB of internal heap is
  left once everything runs.

## 2026-10-12: the panel in its native orientation

Kieran saw the picture scrambled, each row starting where the last one
overran. The CO5300 does not honour MADCTL's row/column exchange: rows
sent 448 pixels wide wrapped onto its native 368 pixel rows. FluidBox
never used the exchange either. The panel is now driven natively (MADCTL
mirrors only, the V2 16 pixel gap back on the native column axis, as in
FluidBox) and the scaler writes each band rotated: a band of 16
landscape rows is 16 native columns by 448 native rows, landscape pixel
(r, x) at index x * 16 + 15 - r, sent with a per-band column window and
a fixed row window. No extra buffer; the scaler's stores become strided
16-bit ones. Which way round it comes out is a guess until seen: the
mirror defines in display.h turn it.

Input, first tried by a person: taps worked only now and then and PWR
not at all, because the input task (priority 3) sat below the render
task (5), which keeps core 0 busy 95% of the time. It runs at 7 now and
polls a steady 50 times a second. Swipes came out as taps because they
were looked for along native x; with the panel in its native
orientation the picture's long side is the touch controller's y.

## 2026-10-12: mode 7, a fused run, and the loops that kept work from it

`teletext_run` renders a run of character words with the SAA5050 state in
locals: a printable byte is a pipe shift, a glyph pointer and two glyph
rows (bg ^ (mask & (fg ^ bg)) a word); a control code writes the state
back, takes `do_data_byte` and reloads. It needs the DISPEN pipeline
settled, so the first word of a line goes the old way.

On the boot bench it took mode 7 from 190 to 133 clocks a word at once;
on mmode7 nothing changed, and the profile (an EVENTS / TTRUN line every
5 s, counts and clocks by event type and for the run) showed why: the
run saw 0.4% of the characters. Two loops in front of it held on to
them. A line's first characters arrive as single ticks while DISPEN
settles (the CRTC fast path steps those exactly), the general loop
took them and, the flags being the same, carried on through every pair
of the line; and the per-word mode 7 loop, once it had taken the first
word, never handed back. Both now return to the run. mmode7: core 0
99% -> 80%, the run taking 2.2 M words per 5 s at 112 clocks each; the
lag is steady. Skip words got cheaper on the way (the teletext pipeline
is not fed once it is at its fixed point, and the beam crosses the
blanking by arithmetic): 472 -> 380 clocks.

Where core 0 goes on mmode7 now, per second: the run about 21%, hsync
26% (most of it the scaler, about 2.6 K clocks a panel row with the
strided stores of the rotated band), skips 6%, queueing bands 6%.

## 2026-10-12: core 1 under budget on the pollers and while loading

The heavy demos were 5 to 15% over the 120-clock budget. Two causes, both
found by measuring rather than in the CPU core, where the last entry
expected them.

The VIA pollers (sotc, mott: `LDA &FE4D : BPL` every 8.6 cycles). PROF's
`io` clock covered only the general access path; the quiet path returned
before it was counted, so its cost had been read as the core's. Timed
(the PROF line's new `quiet` field) it was 354 clocks an access: it
recomputed both VIAs' next events, the CRTC's check horizon and the
1770's, then both VIAs' again to reschedule. All of that is already in
the scheduler's sync point, which is the earliest event of every kind and
can only be moved by an access. The quiet test is now "the access cycle
is before m->sync_due, the 1770 and ADC idle (recorded by the scheduler),
no key queued"; a read keeps the sync point (a read moves no timer
event), a write reschedules from the VIAs as before. 147 clocks an
access. The core's side of an access, looked at in the disassembly, is
about 20 instructions around the call8 and was never the problem.

The old test also had a bug: it let through an access whose cycle was a
VIA event tick (a T1 or T2 underflow on the very cycle of the read) and
stepped it by subtraction, so the flag was never raised and T1 did not
reload. 301 such accesses on sotc's boot, invisible there (T1's interrupt
off, never read), but a loop polling T1's flag diverged from beebjit
10 K cycles in for four latch values out of five. `tools/t1poll.sh` is
that loop, poked into both emulators at cycle 2 M of a no-disc boot
(beebjit `writem` and `e pc=`; `TRACE_JSR` in `host/trace.c`); identical
now. beebjit runs the opcode it had already fetched after `e pc=`, hence
the NOP the test starts with.

Loading (beebstep, teletextr, and twisted between parts, all in DFS's
read loop at &8E1B). The board's profile put the 1770 at 25-31 clocks
per emulated cycle, against 3 on the host. The drive tick (every 64 us)
times the head with beebjit's double arithmetic, two positions and an
index test, and the ESP32-S3 has only a single-precision FPU: 14
soft-float calls a tick. With the standard track length a position's
time is (head * 32 + pulse) * 4 exactly, less one where the doubles
round down; those 341 positions out of 6252, and the first head past the
index pulse, are worked out once at fdc_init with the same expressions,
and checked exhaustively equal. Moving the per-byte path into IRAM as
well (4.2 KB) gave a board that stopped printing once the machine
started; not diagnosed, since flash measured well enough without the
doubles.

Board, normal builds, core 1 clocks per emulated cycle, steady state:

| disc | before | after |
|---|---|---|
| bs-sotc | 132-136 | 111-117 |
| bs-mott | 136-137 | 114-116 |
| bs-beebstep (loading) | 118-120 | 104-105 |
| bs-teletextr (loading) | 120-121 | 106-107 |
| bs-twisted (loading) | 121 | 107 |
| bs-sotc, first 5 s (loading) | 152 | 143 |

What is left on sotc (profile build, 117 clocks): the quiet path 16.5,
the general path 19.4 (6300 accesses per million cycles that meet the
sync point; most of that is the deferred CRTC catching up, work the CRTC
does anyway), the core the rest. Twisted's effect itself was not reached
in a capture this session.

Seen in passing, not yet tested: an IC32 write through the system VIA
that changes the screen size (bits 4 and 5) sets the CRTC's wrap at once,
while that access may have left the CRTC behind, so the deferred run
would fetch the rest of that scanline with the new wrap.

## 2026-10-12: Twisted Brain's twister, and a 1770 divergence it exposed

A 150 s capture of bs-twisted got past the loading: its effects run at
118-128, but the twister (pc &80D2-&8119, about 50-80 s in) ran at 171.
Per scanline it writes the palette four times, then selects and writes
R13 and R12: 67,500 full catch-ups per million cycles, each followed by
a schedule, two for a palette write (the catch-up's own and the one
after the access). Two cuts (host 46.7 -> 36.6 clocks per cycle in that
phase, board 171 -> 151):

- A CRTC register select only latches the address register, read by
  nothing until the data access, which catches up: it takes the quiet
  path with the VIAs and does not reschedule.
- A palette write and an R12/R13 write move nothing the scheduler looks
  at (the CRTC's check horizon depends on R0, R7, the counters and the
  clock rate; the display write window follows the live MA), so before
  the sync point the schedule computed earlier stands.

What is left (profile build, 159): 54,500 catch-ups per million cycles,
each about 260 clocks of catch-up bookkeeping beyond the work, and a
CRTC run cut every 11 ticks on average, so planning (9 clocks per
cycle) and the fetch-run setup are paid 85,000 times per million cycles
and a fast tick costs 30 clocks against 16 in long runs. The two
candidates: a lean catch-up for a write before the sync point (step the
CRTC and the VIAs, no chunk loop), and keeping the fetch run's plan
across a write that cannot change it.

The twister as a frame case at cycle 100 M differs from beebjit by 7800
pixels (28920 before this session's fixes; the same 7800 with and
without the I/O changes above), so it is not in frametest.py yet. Trace
windows bisect the first divergence that persists to cycle 3,531,805
during the boot: a write of &25 to the 1770's control register (&FE24)
while drive 0 spins mid-byte. Both stop and restart the drive timer and
recompute the head position through beebjit's double fraction, in which
head 887 comes back as 886. Ours applies the write on its cycle, so the
next tick reads 886 again; beebjit's drive log reads as if the write
took effect after the tick due at 3,531,818 (887 there, then 888 one
cycle later), where 888 survives the round trip. The phase difference
then shows 59 M cycles later as a sector read 177 cycles late. Why
beebjit orders it that way is in its lazily synced timer countdown
(timing.c), not yet read through.

## 2026-10-12: the core's loop head was the cost; every demo inside the budget

After the twister's catch-ups were cut (a lean catch-up for palette and
R12/R13 writes before the sync point: 54,400 -> 16,000 general catch-ups
per million cycles, 151 -> 138), the profile build was given the time
spent below the core (writes and boundary syncs timed from the core's
side of the call) and the CRTC time inside the lean catch-up. That left
the 6502 core itself at 63 to 80 clocks per emulated cycle in twisted's
heavy phases. Two things it was not: the call into the I/O path
(timed from outside it is within 2% of the inside, so register-window
spills are not the cost) and running from sideways RAM in PSRAM (bank 6,
the twister's code, moved to internal RAM: no change). Nor was it the
CRTC's per-run planning: caching the fast stretch's end across a cut run
(proved exact against a fresh plan on every resumed run over 18 discs,
once the line-start chain and an active cursor were excluded) gained 2
clocks, the profile build's timers having inflated every per-run cost;
it was not kept.

It was the loop head, which runs before every 6502 instruction and
which the compiler copies into every opcode's tail. It tested two
bounds, appended the IRQ history with a variable shift and stored it
back with last_sync, then tested for an interrupt and stored no_poll.
Now there is one bound (min of the run's end and the sync point, kept
by whoever moves the sync point), the history is brought up to date only
where it is used (a catch-up, an access, take_interrupt) since between
syncs the line is constant, and the one-cycle NOPs set i_poll. The
Master's core also compiles its CMOS paths only (1-2 clocks). IRAM text
158.3 -> 150.7 KB.

Board, normal builds, core 1 clocks per emulated cycle:

| disc | before this entry | now |
|---|---|---|
| bs-twisted, the twister | 151 | 102 |
| bs-twisted, the B17x effect | 126 | 80-88 |
| bs-twisted, the ending | 151 | 106-115 |
| bs-twisted, loading | 107 | 96-103 |
| bs-sotc | 111-117 | 77-82 |
| bs-mott | 114-116 | 79-81 |
| bs-beebstep, bs-teletextr (loading) | 104-107 | 93-96 |
| crtc-tmoyos | about 100 | 85 |
| mmode7 | 75-79 | 67 |
| bs-patarty | 156 (first measured) | 79-93 |

With core 1 in time, core 0 is now the tighter one on twisted (up to 88%
in the 8EAx stretch) and on the teletext and loading screens (84-87%).

## 2026-10-12: bs-twisted against beebjit: four differences, one left

The twister frame at cycle 100 M differed by 28920 pixels at the start
of the session and 7800 after the VIA event-tick fix. Debug-mode trace
windows (the oracle always runs with -debug: without it beebjit's drive
log differs, which misled the first look), bisected, then drive-tick,
VIA and CRTC comparisons at the first divergence, found four things,
each now matched:

- ADC status: a 1 MHz read happens here mid cycle; beebjit reads at the
  end of the stretch, after firing timers due on that tick. A
  conversion that ends on the next cycle reads as finished (the demo
  polls exactly 20000 cycles after starting one).
- The 1770's drive moves on by the position whose tick turned the motor
  off (beebjit's callback does not return early); it decides where the
  drive restarts.
- CRTC register writes land at the end of the stretched cycle, one tick
  later than other 1 MHz writes here (beebjit: "required for CRTC").
  The demo rewrites R8 on the tick of a line end.
- Display writes are visible to every CRTC fetch since the end of the
  previous storing instruction (store or read-modify-write, not the
  stack): beebjit's video catches up after each such instruction. A
  write into the display window catches the machine up to that point
  (cpu->last_wr) instead of through the write cycle.

The twister (100 M) and the ending (300 M) are now pixel identical and
frame cases. Still different: the effect at 200 M, 3464 pixels, which
writes display memory as it is shown. Ours also catches the CRTC up at
every line end (the scheduler's check horizon, for vsync detection),
where beebjit does not, so a fetch between such a catch-up and a later
store sees the old byte. Matching it means a check horizon that skips
the line ends that cannot change vsync; that would also cut catch-ups.

Cost on the board (one store per data write): the twister 102 -> 105,
the B17x effect 86 -> 89, the ending 115-116.

## 2026-10-12: a lazier CRTC tried and dropped; three bugs it shook out

To match beebjit at twisted's 200 M frame, the CRTC was made lazy: a
check horizon that skips the line ends that cannot change vsync (they
only change at the half-R0 point of R7's row, at a line end during a
vsync, at the start of R7's row, and on register writes), and catch-ups
that take the CRTC only to the end of the last storing instruction,
where beebjit's video would be, unless a check point or a lag cap
intervened. The "vsync changes found catching up" counter stayed at 0
over 16 discs from power on (900 M cycles) once one bug below was fixed.
It changed nothing at 200 M (the same 3464 pixels), cut boundary syncs
on sotc by two thirds, and cost the drawing effects 1-4 clocks (the
CRTC's runs split at every store point instead). Not kept.

What it shook out, all kept:

- The render task's 8 KB stack came from the internal heap, whose
  largest block is now 7.7 KB; any IRAM growth (the code comes out of
  the same RAM) and it was not created, silently, and the machine then
  waited for ring room for ever. That was also this morning's "1770 in
  IRAM stopped printing". The stack is static, like the machine task's,
  and the other task creations log a failure.
- crtc_ticks_to_check in 1 MHz mode scaled characters to ticks by 2;
  the counters step on odd ticks, so from an odd tick the bound was one
  late (2t - 1, as crtc_fast_ticks has it) and a CRTC left behind could
  run over a vsync change. Latent until something left it behind longer.
- The display write window assumed MA only climbs from the CRTC's
  position; a line end reloads it with the row's start and a frame start
  with R12/R13, so a store to a byte already fetched this line, or in a
  one-line frame, made no sync. The window now includes those starts
  when its span crosses a line end (twisted's drawing effects 89 -> 93).

The 200 M frame's difference is not write timing, catch-up timing or
the window: it does not move with any of them, nor with the dump moment
(beebjit dumped 400 cycles either side). It sits in lines already drawn
at the top of the frame, in an effect that runs one-line frames (R4 0,
R9 0) and rewrites the displayed line as it is shown. Next step there:
compare the fetched bytes themselves (log ours per tick for those lines
and beebjit's from its render) rather than reason about when.

## 2026-10-12: twisted at 200 M was the oracle in -fast mode

A fetch log on both sides (`BEEBJIT_LOG_FETCH`, `FRAME_LOG_FETCH`: tick,
counters, MA and byte for every fetch in a window) showed the same MA
at the same tick with different bytes, and beebjit's byte was one the
CPU stored 24 cycles after the fetch. beebjit installs its memory
written callback (video catch-up after each storing instruction) only
when not in fast mode, and the oracle always runs `-fast`, so its video
moved only on CRTC/ULA writes and its own timer and a store showed back
to whichever came last. Real-time beebjit, what is watched, syncs. The
build script now patches the callback in under `BEEBJIT_MEMSYNC=1` and
frametest sets it. The CPU trace is unchanged by it (2 M cycles
compared). With it: twisted at 180, 190, 199, 199.5, 200 and 210 M
identical; every existing case still 0.

The demo's loader offers "0. EMULATOR (default)" and "1. REAL HARDWARE"
(written before the emulators got these effects right; `?&70`). Every
earlier comparison and the board took the default. With 1 pressed at
12 M, frames at 50-300 M (every 25 M, late in the frame) are identical
except 150 M (176 pixels) and 175 M (24). New cases: twisted2 (200 M),
twisted_hw (125 M) and twisted_hw3 (275 M).

What is left, at 195 M and in hardware mode, is one kind of byte: the
effect reads a display byte, masks it and stores it back (LDA (zp),Y;
AND; ORA; STA (zp),Y). When a VIA event (a 64-tick timer here) falls
between the CRTC's fetch of that byte and the store, our sync runs the
CRTC with the machine and the fetch sees the old byte, which is what
hardware fetches at that tick. beebjit's video does not move on VIA
events, so it renders the fetch after the store and shows the new one.
Matching it means leaving the CRTC behind at every non-video sync, the
lazy CRTC of the entry above (1-4 clocks on the drawing effects, the
ending near the budget), to reproduce a granularity of beebjit's rather
than the hardware. Left as is, noted in PLAN.md.

## 2026-10-13: the ROM flash driver corrupted RAM on every NVS write

The menu clears its NVS flag at boot, and the board crashed right after
(heap metadata reading 0xaaaaa1ea, the lock of a heap region). Heap
integrity checks put it inside nvs_set_u8; the same happened at run time
on a `vol` change (a write that changes the value: the UART driver's
static context was zeroed and the next printf faulted). Swipes and
`boot` had escaped notice because they restart straight after writing.
The cause was CONFIG_SPI_FLASH_ROM_IMPL, set with the other IRAM savings
in Phase 3 on the assumption that nothing writes flash at run time: with
the IDF driver instead, four changing volume writes during a demo leave
the heap intact. It costs 9.4 KB of internal RAM; moving the sound
chip's byte queue (8 KB, a few thousand sequential writes a second) and
the menu's text rows to PSRAM won most of it back: internal heap at
machine start 20727 -> 19871 bytes with SD support and the menu added.
STATS on twisted unchanged.

## 2026-10-13: a sweep of every disc; four CPU/VIA timing fixes

Every disc (NuLA aside) against beebjit at 30 and 60 M cycles, then 15,
45, 90 and 120 M: 252 frames, 10 differing (TheMaster-Demo128 by 52-59 K
pixels, bs-nova-invite by 49 K, then a few hundred or less on wave-runner,
tmoyos, EvlIn11a, evil-inf). Trace windows compared on absolute ticks
(tracediff compares deltas, so a drift of a few cycles passes it) and
bisected found four things, each now matched:

- A T1CH write loads the counter with latch + 1; with a latch of FFFF that
  wrapped the 16-bit counter to 0 and T1 fired at once (TheMaster's
  user VIA, 131 K cycles early). The counter is 32 bits now, firing as it
  passes 0.
- A T1CL or T2CL read on the tick the timer fired leaves the flag set
  (beebjit's via_t1_just_fired). Found by tools/irqpoll.sh, which runs the
  user VIA's T1 interrupt against a polling loop.
- Where an instruction polls the IRQ line around a 1 MHz access: beebjit
  polls plain reads and writes before the access, so a two-cycle stretch
  moves the poll back two cycles; a one-cycle stretch (the access in step
  with the 1 MHz clock the VIA timers change on) leaves it, as a timer
  event on that tick fires before beebjit's poll; read-modify-writes poll
  in their last access (stretch ignored). irqpoll.sh in five addressing
  modes, 20 latch values each, identical.
- An NMI latched within the first three cycles of an IRQ sequence takes
  it over (NMI vector, one sequence; beebjit checks there). Ours decided
  at the start and ran two sequences. The core now syncs at that cycle if
  a sync point is due and checks.

After them all 252 frames are identical, as are the boot traces,
t1poll, every frame case and Klaus. Board: twisted 75-103 clk/cyc as
before.

## 2026-10-13: RVI (evil-inf from 65 s): 242 -> 172 clocks per cycle

The demo's RVI part ran at half speed (242 clocks per emulated cycle on
core 1, budget 120). Per 64 us scanline it writes R0=1 at h 0, so for 32
ticks the 6845 runs two-character lines that step the raster counter
(R4=0, R9=14, R7=3, cursor off), then R0=95 for the rest, R9, R12 and R13
for the next row and two or three palette entries. Host profile of
140-150 M against 100-110 M: 112 K machine syncs per million cycles
against 9 K, and 120 K exact CRTC ticks against 24 K.

- Syncs. crtc_ticks_to_check stopped the machine at every line end in
  case a vsync started there, sixteen times a scanline here. A vsync
  starts only at a line start whose row counter equals R7 (or within a
  vsync, or at the half-R0 point of the R7 row); the counter moves one
  per row end and to 0 at a frame start, and a row is R9 / stride + 1
  lines, so the bound is now the line end at which it could first get
  there (crtc_line_ends_to_r7). Board 242 -> 194; the quiet part of the
  demo 62 -> 57.
- Short lines. With R0 below 8 at 2 MHz, crtc_run hands the ticks to
  crtc_short_lines, which does what crtc_tick does with the counters in
  locals and a plain line end inline (not the row's last line, no
  vsync, adjust or frame end pending: crtc_end_of_line, split out of
  crtc_tick, takes the others), and stops for an hsync, cursor activity
  or a row at R7. A first version with the line end through the shared
  function: 194 -> 177; the line end inline (its row address event
  through the run's ring head, CRTC_RUN_RA): 172. It is noinline: one
  copy in IRAM.

The machine behaves identically (the same sync and tick counts in
host/bench.exe), and frames at 132, 135, 140, 150, 165 and 180 M (the
RVI part) are pixel identical to beebjit, as are the boot traces, every
frame case, the disc sweep at 30 and 60 M, and Klaus. What is left is in
PLAN.md; a split of crtc_tick on the board (fetch and render 138
clocks, chain and half-R0 74, line end 51, line-start checks 123) says
no one thing is the cost.
