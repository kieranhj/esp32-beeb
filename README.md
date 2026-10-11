# esp32-beeb

A BBC Master 128 emulator for the Waveshare ESP32-S3-Touch-AMOLED-1.8, built
to play bitshifters demos on its 1.8-inch 368x448 AMOLED.

Status: planning complete, no emulation code yet. See `PLAN.md` for what is
left and `docs/decisions.md` for why it is shaped this way. `CLAUDE.md` is the
handoff for whoever (or whatever) picks it up next.

## Layout

| Path             | Contents                                                   |
| ---------------- | ---------------------------------------------------------- |
| `main/`          | ESP-IDF application (skeleton only for now)                |
| `discs/`         | Target disc images: bitshifters `.ssd` set plus NICCC `.dsd` |
| `roms/mos3.20/`  | Master 128 MOS 3.20 ROM set (Acorn copyright, do not redistribute) |
| `tools/`         | `capture.py` serial capture; test and bundle tools to come |
| `docs/`          | Decisions log                                              |
| `partitions.csv` | 3 MB app plus a 12 MB raw `discs` partition                |

## Building

ESP-IDF 5.5.5. On this machine it is installed under `C:\esp\v5.5.5` and is
activated per shell with:

```powershell
. C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1
idf.py -p COM3 flash monitor
```

The board enumerates as a USB Serial device on COM3. `Ctrl-]` leaves the
monitor. `python tools/capture.py 8 COM3` captures eight seconds of boot log
non-interactively.

## Licence

The emulator code is MIT (to be added with the first real code). The ROM
images are Acorn's and the disc images belong to their authors.
