# esp32-beeb

A BBC Master 128 emulator for the Waveshare ESP32-S3-Touch-AMOLED-1.8, built
to play bitshifters demos on its 1.8-inch 368x448 AMOLED.

Status: it plays them. A cycle-stepped 65C12 and Master on core 1, beebjit's
video model rendering and scaling on core 0, the 1770 and discs from a flash
bundle, SN76489 sound through the ES8311, touch and the PWR button. On the PC
it traces cycle-identical to beebjit and renders pixel-identical frames. See
`PLAN.md` for what is left and `docs/decisions.md` for the numbers and why
it is shaped this way. `CLAUDE.md` is the handoff for whoever (or whatever)
picks it up next.

## Layout

| Path             | Contents                                                   |
| ---------------- | ---------------------------------------------------------- |
| `main/`          | ESP-IDF application: the machine (`machine.c`, `cpu6502.c`, `crtc.c`, `via.c`, `fdc.c`, `disc.c`), video (`render.c`, `teletext.c`, `video_esp.c`, `display.c`), sound (`sn76489.c`, `sound_esp.c`), input, console, disc bundle |
| `host/`          | Desktop harnesses: trace and frame tools for the beebjit diffs, benches, sound to WAV, Klaus Dormann CPU tests |
| `discs/`         | Target disc images: bitshifters `.ssd` set plus NICCC `.dsd` |
| `roms/mos3.20/`  | Master 128 MOS 3.20 ROM set (Acorn copyright, do not redistribute) |
| `tools/`         | Frame tests, trace diff, native beebjit build (the oracle), disc bundle builder, board console client, serial capture |
| `docs/`          | Decisions log                                              |
| `partitions.csv` | 3 MB app plus a 12 MB `discs` partition for the bundle     |

## Building and running

ESP-IDF 5.5.5. On this machine it is installed under `C:\esp\v5.5.5` and is
activated per shell with:

```powershell
. C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1
idf.py -p COM3 flash
python tools/make_disc_bundle.py --flash COM3     # once: every disc in discs/
python tools/beeb.py COM3 boot bs-twisted.ssd     # choose the disc (kept across resets)
python tools/capture.py 30 COM3                   # boot log and STATS lines
```

On the board: touch holds Space, a long press adds Return, a swipe along the
long side changes disc, a short PWR press restarts, a 2 s hold turns the sound off or on, a 6 s hold powers off.

## Licence

The emulator code is MIT (to be added). The ROM images are Acorn's and the
disc images belong to their authors.
