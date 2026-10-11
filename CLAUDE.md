# CLAUDE.md

Guidance for an agent picking this project up. Read `PLAN.md` next, then
`docs/decisions.md`. Both are short. Keep all three current as you work: PLAN.md
is only what is left, decisions.md is why, this file is how.

## What this is

A cycle-stepped BBC Master 128 emulator written for the ESP32-S3 inside the
Waveshare ESP32-S3-Touch-AMOLED-1.8, to play the bitshifters demos in `discs/`
on its 368x448 AMOLED. Accuracy of CRTC/ULA tricks is the goal. It is not a
port: beebjit is the reference and test oracle, not the engine. Phase 0
(measure host clocks per emulated cycle on the real board) is done and the
numbers are in `docs/decisions.md`; the cycle-stepped design stands. What
exists: `main/cpu6502.c`, a 65C12 (the Master's CPU; NMOS mode too) with
exact bus sequencing, and `main/main.c`, which is still the Phase 0
benchmark harness. Phase 1 is under way.

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
out/beebjit-win/beebjit.exe -master -mode interp -headless -fast -debug -print     -commands "breakat 1000000;c;q" > trace.txt   # 1 M cycles of Master boot
```

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
in DRAM (`.dram1.N`), not in `.flash.text` / `.flash.rodata`. Disassembly:
`C:/Espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump.exe -d build/esp32-beeb.elf`.
Look for `jx` to confirm table dispatch and for `callx8` inside the run loop
to catch helpers that failed to inline.

The core is built in several variants by tiny wrapper files (`main/cpu_*.c`)
that set `CPU6502_FN`, `CPU6502_CODE_ATTR`, `CPU6502_TABLE_ATTR`,
`CPU6502_TICK` and then `#include "cpu6502.c"`. That is how the per-cycle
hook is inlined without a function pointer, and how Phase 0 compared
placements. Phase 1 keeps one variant and hangs the machine off the hook.

`sdkconfig` already exists, so a change to `sdkconfig.defaults` does not take
effect until it is also made in `sdkconfig` (or `sdkconfig` is deleted and
regenerated).

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

- ESP32-S3, dual Xtensa LX7 at 240 MHz, 512 KB SRAM (about 380 KB usable,
  the skeleton prints the real figure at boot), 8 MB octal PSRAM, 16 MB QIO
  flash. Icache 32 KB and dcache 64 KB are configured in `sdkconfig.defaults`.
- PSRAM is cached but a miss costs tens of cycles; the emulation inner loop
  must touch only internal SRAM, IRAM and flash-cached ROM.
- Panel: CO5300 over QSPI at 80 MHz, RGB565, 368x448 portrait. FluidBox
  sustains 100 fps full-frame with 28-row band DMA from two internal buffers.
- Inputs: capacitive touch (CST820 at I2C 0x15), PWR button, QMI8658 IMU.
  No keyboard. Audio: ES8311 codec over I2S with a speaker amp.

## Working rules for this repo

- Phase order in `PLAN.md` is deliberate. Any change to the run loop's
  structure (state in locals, computed-goto dispatch, 32-bit cycle counter)
  gets re-measured with the Phase 0 harness before it is kept; the harness
  stays in git history at the Phase 0 commit.
- Core emulation files (`cpu6502.c`, `via.c`, `crtc.c`, `ula.c`,
  `machine.c`) must compile on a desktop C compiler with no ESP headers, so
  trace and frame diffs against beebjit run on the PC.
- Every accuracy claim gets a diff against beebjit, not a visual check. The
  Klaus Dormann tests (`sh host/build.sh`) must keep passing after any CPU
  change; they take under a second.
- Commit after each ticked item in `PLAN.md` with a message saying what
  changed and the measured effect if any.
- Nothing from `discs/` or `roms/` is to be published; the discs belong to
  their authors and the ROMs to Acorn.
