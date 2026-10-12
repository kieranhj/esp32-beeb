# CLAUDE.md

Guidance for an agent picking this project up. Read `PLAN.md` next, then
`docs/decisions.md`. Both are short. Keep all three current as you work: PLAN.md
is only what is left, decisions.md is why, this file is how.

## What this is

A cycle-stepped BBC Master 128 emulator written for the ESP32-S3 inside the
Waveshare ESP32-S3-Touch-AMOLED-1.8, to play the bitshifters demos in `discs/`
on its 368x448 AMOLED. Accuracy of CRTC/ULA tricks is the goal. It is not a
port: beebjit is the reference and test oracle, not the engine. It works:
the demos boot from a flash bundle and play with picture, sound and touch.
What is left is speed on the heaviest demos and the items in `PLAN.md`.

Layout: `main/cpu6502.c` (65C12 core), `main/machine.c` (the Master; it
includes `via.c`, `crtc.c`, `disc.c`, `fdc.c` and `cpu6502.c` into one
translation unit), `main/render.c` + `main/teletext.c` (stream to pixels,
beebjit's beam model), `main/sn76489.c` (sound chip). Board-only:
`main/main.c` (tasks, pacing loop, STATS), `machine_esp.c` (placement),
`video_esp.c` (render task, scaler, band handling), `display.c` (CO5300),
`sound_esp.c` (ES8311), `input.c` (touch, PWR), `console.c` (serial
commands), `bundle.c` (disc partition). Host harnesses in `host/`.

The user is Kieran, a bitshifters member who wrote several of these demos and
built the beebjit and b2 libretro cores in `../rg40xxv`. Assume deep BBC Micro
knowledge; explain ESP32 specifics, not 6502 or 6845 ones.

## Environment

- Windows 11, PowerShell. ESP-IDF v5.5.5 installed by Espressif's `eim` at
  `C:\esp\v5.5.5\esp-idf`, tools in `C:\Espressif`. Activate per shell:

  ```powershell
  . C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1
  ```

  From Git Bash there is no activation script; use PowerShell for `idf.py`.
- The board is on `COM3` (USB-Serial-JTAG, 115200). It re-enumerates on
  reset, which is why `tools/capture.py` reopens the port.
- `python -m esptool` is also installed in the system Python for raw flashes.

## Commands

Host build of the core (no ESP-IDF needed) and the Klaus Dormann tests, from
Git Bash. There is no gcc on this machine; `zig cc` (clang) is on PATH:

```sh
sh host/build.sh                 # builds host/cputest.exe, runs the five Klaus tests
sh tools/build_beebjit_win.sh    # native beebjit.exe into out/beebjit-win/ (the oracle)
zig cc -O2 -std=c11 -Wall -Wextra -Wno-unused-function -o host/trace.exe host/trace.c
(cd out/beebjit-win && ./beebjit.exe -master -mode interp -headless -fast -accurate \
    -debug -print -opt debug:print-ticks -commands "breakat 2000000;c;q") > out/bj.txt
host/trace.exe roms/mos3.20 2000000 > out/ours.txt
python tools/tracediff.py out/bj.txt out/ours.txt     # first divergence, with context
TRACE_DEBUG=1 host/trace.exe roms/mos3.20 2000000 2>&1 >/dev/null | head   # VIA flags per IRQ
```

`-accurate` on the beebjit side is not optional: without it the VIAs run on
wall time and the trace is irreproducible. beebjit's debugger `eval` of VIA
registers (`sysvia_r[n]`) returns stale timer values; use the trace instead.

Sound on the host: `zig cc -O2 -std=c11 -o host/sound.exe host/sound.c`,
then `host/sound.exe roms/mos3.20 4000000 out/boot.wav` (the boot beep) or
with `--disc x.ssd --autoboot`; it writes a 31250 Hz WAV from the SN76489
model and prints the bytes landed, the peak and, per half second, the RMS
and a zero-crossing frequency, so a result reads without listening.
`SOUND_DEBUG=1` prints every byte with the chip's registers after it.

Frame diffs (pixels against beebjit's canvas):

```sh
zig cc -O2 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-but-set-variable \
    -Wno-unused-parameter -Wno-unused-variable -o host/frame.exe host/frame.c
python tools/frametest.py             # all cases; add names to run some; --trace adds a trace diff
python tools/framecrop.py ref.bin ours.bin out.png   # the differing region, reference above ours
host/frame.exe roms/mos3.20 2000000 out/f.bin --key <cycle> <row> <col> <down> --crtc <cycle>
```

`host/frame.exe` renders our video stream the way beebjit renders, dumps the
768x640 canvas, and can press keys at exact cycles, poke RAM, print CRTC
counters and beam positions (`--crtc`, with `FRAME_DEBUG=1`), write a
trace (`--trace`, from a cycle with `--trace-from`), put a disc in a drive
(`--disc`, `--disc1`) and autoboot it (`--autoboot`; beebjit: `-0 disc
-autoboot`). `host/trace.exe` takes `--disc` and `--autoboot` too. The
beebjit build script also patches in `BEEBJIT_LOG_NMI=1` and
`BEEBJIT_LOG_DRIVE=1`, which print every 1770 NMI edge and drive tick with
its cycle to stderr; `host/trace.exe` built with `-DMACHINE_DEBUG_FDC
-include stdio.h` prints ours. beebjit's debugger `p` toggles instruction
printing, so a trace of a late window is `breakat N;c;p;breakat M;c;q`. `sh tools/t1poll.sh host/trace.exe <latch>` pokes a T1-polling loop
into both emulators (beebjit `writem`/`e pc=`, ours `TRACE_JSR`) and
diffs the traces: the regression test for VIA accesses that land on a
timer event; run it for a few latch values. `TRACE_FROM=<cycle>` limits
our trace to a window. `tools/frametest.py` drives both emulators through the
keyboard matrix; its cases are the regression set for video changes. Build
it a second time with `-DRENDER_INDEXED` (the board's pixel type) as
`host/frame_idx.exe` and run the bitmap cases with
`FRAME_EXE='host\frame_idx.exe'`: they must still be identical; mode 7
differs only in beebjit's anti-aliased glyph edges. The board's pixel is a
colour code resolved by the emitter (`main/pixel.h`): the indexed host
build resolves the same way into its canvas, so a palette-timing change
is checked by the indexed cases, not only the ARGB ones.

On the board (`idf.py -p COM3 flash`, then `python tools/capture.py 45 COM3`):
boot prints BENCH lines (the renderer and panel path timed over synthetic
MODE 2 and MODE 7 streams with core 1 idle), then every 5 s a PANEL line
(rows scaled, time waiting for a band buffer, time queueing bands) and a
STATS line (core 1 clocks per emulated cycle, how far behind real time,
frames and bands sent, stream overruns, core 0 render load and ns per
stream word), and a VIDEO line (renderer events, palette writes, table
rebuild time, time in slow words). Build with `-DBEEB_MEASURE` (add to
`target_compile_options` in `main/CMakeLists.txt`) to get the first 15 s
split into no stream / stream dropped / rendered unsent.

Discs on the board: `python tools/make_disc_bundle.py --flash COM3` packs
every image in `discs/` into `out/discs.bin` and writes it to the `discs`
partition (0x310000; do this once, it survives app flashes). Then
`python tools/beeb.py COM3 list` and `python tools/beeb.py COM3 boot
bs-twisted.ssd`: the console task (`main/console.c`, reading the USB
serial port directly) stores the choice in NVS and resets; the board
autoboots that disc from then on, or the bundle's first if none is
chosen. Without a bundle it falls back to the one disc embedded in the
image: `idf.py "-DBEEB_DISC=x.ssd" build` (quote it: PowerShell splits an
unquoted `-D` value at the dot and CMake then wipes its cache; the choice
sticks in `build/CMakeCache.txt`, default `crtc-tmoyos.ssd`).
`BEEB_TYPE_DEMO` types a BASIC line drawer instead of booting a disc.

Profiling the machine on the board: build with `-DBEEB_PROFILE` and
`VS_RING=8192` (both in `main/CMakeLists.txt`; the counters cost 13 KB of
IRAM and the ring is halved to make room). It prints a PROF
line every 5 s: calls and chunks per million cycles, clocks per emulated
cycle in the catch-up split into CRTC (planning, fast ticks, exact ticks),
VIAs, 1770 and scheduling, the I/O paths, and SHEILA accesses by page with
the handler's cost. The Xtensa cycle counter is a single instruction, so
the split is honest. The host bench prints the same counters, with host
clocks, for a window of a demo: `host/bench.exe roms/mos3.20 32000000
--disc discs/x.ssd --autoboot --from 30000000`; counts transfer to the
board, proportions do not (the board pays about five times the clocks).
The CRTC's exact ticks are counted by cause there (`exact ticks per 1M`).

Board:

```powershell
idf.py set-target esp32s3 build          # first build fetches components
idf.py -p COM3 flash                     # app + bootloader + partition table
idf.py -p COM3 monitor                   # Ctrl-] to exit
python tools/capture.py 8 COM3           # 8 s of boot log, scriptable
idf.py size-components                   # where flash/IRAM/DRAM went
```

Map file for placement checks: `build/esp32-beeb.map`. Confirm hot functions
landed in IRAM (`.iram1.N` input sections under `.iram0.text`) and hot tables
in DRAM (`.dram1.N`), not in `.flash.text` / `.flash.rodata`. IRAM text
has a hard limit of about 168 KB: past it the link fails with `dangerous
relocation: j: cannot encode: xt_debugexception` (the debug vector's jump
cannot reach). The core is 100 KB of it; the renderer runs from flash by
choice (`RENDER_FN` in `video_esp.c`), which measured the same. Disassembly:
`C:/Espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump.exe -d build/esp32-beeb.elf`.
Look for `jx` to confirm table dispatch and for `callx8` inside the run loop
to catch helpers that failed to inline.

The core is built by including it: `machine.c` defines `CPU6502_TICK`,
`CPU6502_IO_RD/WR` and the `CRTC_*` hooks, then includes `via.c`, `crtc.c`
and `cpu6502.c`, so the per-cycle hook, the CRTC step and the I/O paths
inline into the core with no function pointers. The Phase 0 benchmark
(`main/cpu_*.c`) builds the bare core the same way with different placement
attributes. `host/trace.c` includes `machine.c` with `CPU6502_TRACE`
defined. Keep that shape: one translation unit per machine.

`sdkconfig` already exists, so a change to `sdkconfig.defaults` does not take
effect until it is also made in `sdkconfig` (or `sdkconfig` is deleted and
regenerated).

Measuring: `machine_profile_skip` (bit 0 no CRTC, bit 1 no VIAs, bit 2 no
catch-up) and `-DBEEB_MEASURE` still exist for splitting costs; the
profile build (`-DBEEB_PROFILE`, above) is the usual tool now. `host/bench.exe
roms/mos3.20 20000000` prints the same counters on the PC in a second; use
it to see what a change does to call and chunk counts before flashing.
Timings on the board are deterministic to the microsecond, so a single run
is a measurement.

Restoring other firmware on the board, from their own project folders:
FluidBox is `../esp32-fluidbox/fluidbox` (`idf.py -p COM3 flash`); the stock
Brookesia firmware is a whole-image flash of
`../ESP32-S3-Touch-AMOLED-1.8/firmware/ESP32-S3-Touch-AMOLED-1.8-V2-FactoryOnly-260915.bin`
at offset 0. This is a V2 board (CO5300 panel, CST820 touch).

## Reference code, all local

- `../rg40xxv/beebjit/` : beebjit source. `interp.c` is the accurate 6502
  reference; `via.c`, `video.c`, `render.c`, `teletext.c`, `wd_fdc.c`,
  `sound.c` are the hardware references; `make_test_rom.c` and
  `make_timing_rom.c` generate test ROMs; `test-video.c` and `test-timing.c`
  encode known-correct timings. `bbc.c` lines 600-800 show the Master
  paging rules. ROMs under `roms/`.
- `../rg40xxv/beebjit-libretro/core.c` : how beebjit was driven in lockstep
  per frame, and the Master ROM slot table (also in `roms/README.md` here).
- `../esp32-fluidbox/fluidbox/main/` : working panel bring-up for this exact
  board (`display.c`, band DMA, V2 detection, 16-pixel gap), IMU (`imu.c`),
  PWR button (`button.c`), and `sdkconfig.defaults` that is known good.
- `../ESP32-S3-Touch-AMOLED-1.8/` : Waveshare's repo. `firmware/brookesia/`
  has the ES8311 audio path (`components/bsp_extra`) and SD mounting
  (`components/storage_service`) for Phase 4. Its `docs/FIRMWARE.md` lists
  boot-critical memory settings for this board; the LVGL ones do not apply
  here but the PSRAM/DMA buffer lesson does: DMA buffers must be internal.
- Host-side beebjit for oracle runs: there is no Windows build yet. Options
  are `build_win_opt.sh` in the beebjit tree (needs clang) or WSL. Set this up
  early in Phase 1; trace diffs over serial are too slow to iterate on.

## Hardware facts that matter

- ESP32-S3, dual Xtensa LX7 at 240 MHz, 512 KB SRAM, 8 MB octal PSRAM, 16 MB
  QIO flash. Icache 32 KB and dcache 64 KB (`sdkconfig.defaults`).
- PSRAM is cached but a miss costs tens of cycles; the emulation inner loop
  touches only internal SRAM, IRAM and flash-cached ROM.
- Internal RAM is the scarce resource: about 18 KB of heap is left once
  everything runs. The machine task's 16 KB stack is static for that
  reason (a heap-allocated one silently failed to be created). Check
  `idf.py size` (DIRAM) after adding anything resident.
- IRAM text has a hard limit of about 168 KB (the link fails with
  `dangerous relocation: j: cannot encode: xt_debugexception`). The core is
  100 KB of it; the renderer runs from flash by choice.
- Do not use `esp_partition_mmap` for the disc bundle: on this build it
  handed out addresses inside the PSRAM heap's range and corrupted it.
  `bundle.c` reads with `esp_partition_read`.
- Panel: CO5300 over QSPI at 80 MHz, RGB565, 368x448 native portrait. It
  ignores MADCTL's row/column exchange; bands are written rotated (see
  `display.h`). V2 board: 16 pixel gap on the native column axis.
- Inputs: CST820 touch at I2C 0x15 (register 0x00: fingers in byte 2, x/y
  in bytes 3-6; the picture's long side is its y), PWR on the TCA9554
  expander at 0x20 (EXIO4, high while held; a 6 s hold cuts power in the
  AXP2101 at 0x34). No keyboard. Audio: ES8311 over I2S, amplifier enable
  GPIO 46; the esp_codec_dev driver only knows standard rates (31250 Hz
  played silence), so it runs at 32 kHz.
- Task priorities on core 0 matter: the render task (5) is busy 80-99% of
  the time, so anything that must run on time sits above it (input 7,
  sound 6).

## Working rules for this repo

- Measure before and after any performance change, on the board (STATS,
  PROF with `-DBEEB_PROFILE`, the EVENTS/TTRUN renderer profile) and with
  `host/bench.exe` for counts; write the numbers in the commit message and
  in `docs/decisions.md`. The board is deterministic enough that one run
  is a measurement.
- Core emulation files (`cpu6502.c`, `via.c`, `crtc.c`, `machine.c`,
  `fdc.c`, `disc.c`, `render.c`, `teletext.c`, `sn76489.c`) must compile on
  a desktop C compiler with no ESP headers, so trace and frame diffs
  against beebjit run on the PC.
- Before committing a change to the machine, CPU or video: the disc boot
  trace and the no-disc trace against beebjit, all frame cases
  (`python tools/frametest.py`, and the indexed build), and the Klaus tests.
- Every accuracy claim gets a diff against beebjit, not a visual check. The
  Klaus Dormann tests (`sh host/build.sh`) must keep passing after any CPU
  change; they take under a second.
- Commit after each ticked item in `PLAN.md` with a message saying what
  changed and the measured effect if any.
- Nothing from `discs/` or `roms/` is to be published; the discs belong to
  their authors and the ROMs to Acorn.
