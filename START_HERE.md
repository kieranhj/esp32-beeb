# esp32-beeb: start here

A cycle-accurate BBC Master 128 emulator for the Waveshare ESP32-S3-Touch-AMOLED-1.8 (ESP-IDF, C), written to play bitshifters demos on its 368x448 AMOLED. beebjit is the reference and test oracle. It works on the board; the remaining work is speed on the heaviest demos.

| | |
|---|---|
| GitHub | https://github.com/kieranhj/esp32-beeb (public) |
| Local path | D:\pico-esp32\mine\esp32-beeb (moved from C:\Users\khcon\OneDrive\Projects\esp32-beeb on 2026-11-21) |
| Branch | `main`; other branches: `wip/2026-11-tidy` |
| Last commit | 2026-10-14: Paging reads through the quiet window (evil-inf 355-360 s 121 -> 81) |

## What's in here
- `main/`: the emulator. Portable core: `cpu6502.c`, `machine.c` (includes `via.c`, `crtc.c`, `disc.c`, `fdc.c`), `render.c`, `teletext.c`, `sn76489.c`. Board-only: `main.c`, `video_esp.c`, `display.c`, `sound_esp.c`, `input.c`, `console.c`, `bundle.c`, `sdcard.c`, `menu.c`.
- `host/`: PC harnesses built with `zig cc`: `cputest` (Klaus Dormann tests), `trace`, `frame`, `sound`, `bench`.
- `tools/`: `frametest.py` / `discsweep.py` / `tracediff.py` (diffs against beebjit), `build_beebjit_win.sh`, `make_disc_bundle.py`, `beeb.py` / `capture.py` (serial control of the board).
- `discs/` (46 images) and `roms/`: not in git (gitignored; not to be published).
- `docs/decisions.md`; `out/`, `build/`, `managed_components/`: generated, gitignored.

## Build and run
- `make`: host build of the CPU core and the Klaus Dormann tests via `host/build.sh` (Git's sh + `zig cc`); prints "all CPU tests pass". Other targets: `make firmware` (`idf.py build`), `make size`, `make help`.
- `make firmware` activates ESP-IDF v5.5.5 (`C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1`, installed) and runs `idf.py build`. **Right now it stops on purpose**: `build\` was configured at the old OneDrive path and CMake will refuse it. Delete `build\` (gitignored) first. `BEEB_DISC` (default `crtc-tmoyos.ssd`) is stored in that cache, so set it again if it was changed. The firmware build has not been tried at the new path yet.
- Flash and monitor by hand from an activated PowerShell: `idf.py -p COM3 flash`, `python tools/capture.py 8 COM3`. Discs: `python tools/make_disc_bundle.py --flash COM3` once, then `python tools/beeb.py COM3 boot <disc>.ssd`.
- Oracle: `sh tools/build_beebjit_win.sh` builds beebjit from D:\misc\rg40xxv\beebjit into `out/beebjit-win/`. CLAUDE.md has the trace, frame and sound diff commands.

## Local changes
- Path fixes for the D: move (2026-11-22, uncommitted): `host/build.sh` and `tools/build_beebjit_win.sh` now use `../../../misc/rg40xxv/...` (was `../rg40xxv`); `tools/t1poll.sh` and `tools/irqpoll.sh` `cd` to the repo root instead of the old OneDrive path.
- Uncommitted from before: `tools/boardsweep.py` (untracked). `wip/2026-11-tidy` (pushed) is a snapshot of that file.
- CLAUDE.md still names sibling folders as `../rg40xxv`, `../esp32-fluidbox`, `../ESP32-S3-Touch-AMOLED-1.8`. They are now D:\misc\rg40xxv, D:\pico-esp32\third-party\esp32-fluidbox and D:\pico-esp32\third-party\ESP32-S3-Touch-AMOLED-1.8.

## Status and next steps
- PLAN.md "Status": all demos play on the board, pixel-identical to beebjit (NuLA discs aside); core 1 is within budget (65-117 clocks per cycle out of 120).
- Next (PLAN.md): core 0 load on teletext and loading screens, then the SN76489 write cycle, then frame cases for the rest of `discs/`.
- Working rules (CLAUDE.md): measure before and after every performance change; Klaus tests, traces and frame cases before committing core changes.

## See also
- CLAUDE.md (how), PLAN.md (what's left), docs/decisions.md (why), README.md.
- D:\misc\rg40xxv (beebjit, b2-libretro); D:\misc\mine\dev-scraps\waveshare-beeb\PLAN.md (the original plan).
