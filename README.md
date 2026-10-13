# esp32-beeb

A cycle-accurate BBC Master 128 emulator for the
[Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm),
a credit-card-sized board with a 368×448 AMOLED screen. It was built to play
the demos of the [bitshifters collective](https://bitshifters.github.io/) and
friends, the kind that bend the 6845 CRTC and the video ULA in ways a "good
enough" emulator gets wrong.

The goal is accuracy first. The emulator is written for this chip rather than
ported, and [beebjit](https://github.com/scarybeasts/beebjit) is its reference:
every change is checked against beebjit by instruction trace and by
pixel-for-pixel frame comparison on a PC, not by eye.

## What it does

- **A cycle-stepped 65C12 and Master 128 on one core.** The CPU, both 6522
  VIAs, the 1 MHz bus stretch, the 6845 CRTC, the video ULA, the 1770 floppy
  controller with its drives, the SN76489 sound chip, CMOS RAM, shadow and
  sideways RAM. It runs in real time on core 1 of the ESP32-S3 (240 MHz),
  within a budget of 120 host clocks per emulated 2 MHz cycle; the heaviest
  demos use 75 to 117.
- **beebjit's video model on the other core.** Core 0 turns the CRTC's output
  into pixels with beebjit's beam model (bitmap modes, teletext, mid-line
  palette and mode changes), scales the 640×256 picture to the panel's
  448×336 and streams it to the panel in bands, with no frame buffer.
- **Sound** through the board's ES8311 codec and speaker.
- **Discs** from a bundle written to flash, or from a microSD card.
- **Touch controls and a disc menu**, so it can be used without a keyboard.

### Accuracy

On the PC, the emulator core builds without any ESP-IDF code and runs side by
side with a native build of beebjit:

- The power-on and disc boot traces are instruction-for-instruction and
  cycle-for-cycle identical to beebjit (6 M cycles with a disc, 2 M without).
- Every disc in the test set (42 demos, the NuLA ones aside) renders
  pixel-identical frames to beebjit at 15, 30, 45, 60, 90 and 120 million
  cycles from boot.
- Targeted regression tests cover VIA timer edges, interrupt polling around
  1 MHz accesses in each addressing mode, and the screen modes.
- The Klaus Dormann 6502/65C02 functional tests pass.

## The board

Waveshare ESP32-S3-Touch-AMOLED-1.8 (the V2 board: CO5300 panel, CST820
touch). Dual-core ESP32-S3 at 240 MHz, 8 MB octal PSRAM, 16 MB flash, an
ES8311 codec with speaker, a QMI8658 motion sensor, an AXP2101 power chip with
a battery connector, and a microSD slot.

## Controls

| Input | Action |
| --- | --- |
| Touch and hold | Space, for as long as your finger is down |
| Long press (over 0.8 s) | Return, as well |
| Swipe along the long side | Next or previous disc (restarts) |
| Swipe across the short side | Open the disc menu |
| Shake | Open the disc menu |
| PWR, short press | Restart (the disc autoboots, as Shift+BREAK) |
| PWR, hold 2 s | Sound off or on |
| PWR, hold 6 s | Power off (the AXP2101 does this itself) |

In the disc menu: tap a disc to highlight it, tap it again to boot it, swipe
along the long side to change page, and swipe across or press PWR to leave.
Discs from the SD card are shown in green, and the current disc is marked
with `>`.

Some demos ask a question at boot. The board answers for the ones it knows
(bs-twisted is told "1. REAL HARDWARE"); the table is `s_boot_keys` in
`main/main.c`.

## What you need to supply

This repository contains no ROM or disc images: the ROMs are Acorn's and the
discs belong to their authors.

- **ROMs:** the Master 128 MOS 3.20 set in `roms/mos3.20/` (`mos.rom`,
  `dfs.rom`, `basic4.rom`, `adfs.rom`, `edit.rom`, `view.rom`, `viewsht.rom`,
  `terminal.rom`). beebjit and BeebEm carry this set. See
  [`roms/README.md`](roms/README.md) for the slot layout.
- **Discs:** `.ssd` or `.dsd` images in `discs/`. One is built into the
  firmware as a fallback (`crtc-tmoyos.ssd` by default, or choose with
  `-DBEEB_DISC=name.ssd`), and all of them go into the flash bundle. The
  bitshifters demos are free to download from
  [their site](https://bitshifters.github.io/).

## Building and flashing

You need [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) 5.5. The
commands below are for PowerShell on Windows, the platform this was developed
on; on Linux or macOS use `export.sh` and your serial port instead.

```powershell
. C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1   # activate ESP-IDF
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash
```

To pick the built-in fallback disc, quote the option (PowerShell splits an
unquoted `-D` value at the dot): `idf.py "-DBEEB_DISC=bs-twisted.ssd" build`.

### Getting discs onto the board

**Flash bundle:** every image in `discs/` is packed into one file and written
to the 12 MB `discs` partition. You only need to do this once; it survives
firmware updates.

```sh
python tools/make_disc_bundle.py --flash COM3
```

**microSD card:** put `.ssd` and `.dsd` files on a FAT-formatted card, either
in the root or in a `discs` folder. At boot they join the list, and a card
disc with the same name as a bundled one replaces it, so an updated disc only
has to go on the card.

### The serial console

The board listens on its USB serial port. `tools/beeb.py` sends a command and
prints the reply:

```sh
python tools/beeb.py COM3 list                  # every disc, bundle and card
python tools/beeb.py COM3 boot bs-twisted.ssd   # choose a disc (kept across restarts)
python tools/beeb.py COM3 menu                  # open the disc menu on the screen
python tools/beeb.py COM3 vol 70                # speaker volume 0-100 (kept)
python tools/beeb.py COM3 reset
python tools/capture.py 30 COM3                 # 30 s of log, including STATS lines
```

Every 5 seconds the board prints a `STATS` line: host clocks per emulated
cycle on core 1 (under 120 keeps up with real time), frames and panel bands
sent, and core 0's render load.

## Testing against beebjit on a PC

The emulator core (`cpu6502.c`, `machine.c`, `via.c`, `crtc.c`, `fdc.c`,
`disc.c`, `render.c`, `teletext.c`, `sn76489.c`) is plain C with no ESP-IDF
dependencies, so the same code runs in host harnesses. On Windows these build
with `zig cc`, the setup used here; any C11 compiler should do.

```sh
sh tools/build_beebjit_win.sh       # a patched native beebjit, the reference
sh host/build.sh                    # Klaus Dormann CPU tests
python tools/frametest.py           # screen-mode, palette, teletext and demo frames vs beebjit
python tools/discsweep.py 30000000,60000000   # every disc vs beebjit at those cycles
sh tools/irqpoll.sh host/trace.exe 10 12 lda  # interrupt timing around 1 MHz accesses
```

`host/trace.exe` writes instruction traces in a format `tools/tracediff.py`
compares with beebjit's. `host/frame.exe` renders frames exactly as beebjit
does and dumps them for `tools/framediff.py`. It can also press keys at exact
cycles and log every display fetch or vsync edge, for finding the byte or the
cycle where two frames part. `CLAUDE.md` has the full set of commands.

## Repository layout

| Path | Contents |
| --- | --- |
| `main/` | The ESP-IDF application. The machine: `cpu6502.c`, `machine.c`, `via.c`, `crtc.c`, `fdc.c`, `disc.c`, `sn76489.c`. Video: `render.c`, `teletext.c`, `video_esp.c`, `display.c`. Board: `main.c`, `sound_esp.c`, `input.c`, `console.c`, `bundle.c`, `sdcard.c`, `catalog.c`, `menu.c` |
| `host/` | PC harnesses: traces, frames, benchmarks, sound to WAV, CPU tests |
| `tools/` | Frame and trace comparison, the beebjit build script, disc bundle builder, board console and log capture |
| `docs/decisions.md` | Why it is built the way it is, with the measurements behind each decision |
| `PLAN.md` | What is left to do |
| `CLAUDE.md` | Working notes: environment, commands and the rules the project follows |
| `roms/`, `discs/` | Where you put the ROM and disc images (not included) |

## Status and limits

It plays the bitshifters demos and the rest of the test set on the board, with
picture, sound and touch. What is left is in [`PLAN.md`](PLAN.md): mostly
render load on core 0 for teletext and loading screens, and the sound chip's
write timing for sample players. Out of scope for now: VideoNuLA, the Model B,
tape, the Tube, ADFS and hard discs, and the Music 5000.

## Credits

- [beebjit](https://github.com/scarybeasts/beebjit) by Chris Evans: the
  reference for every part of the machine, and the test oracle.
- The teletext character set (`main/teletext_glyphs.h`) is from
  [B-em](https://github.com/stardot/b-em) by Sarah Walker and others.
- Klaus Dormann's 6502 functional tests.
- The demos: [bitshifters](https://bitshifters.github.io/) and the authors of
  the other discs in the test set.

## Licence

GPL-3.0; see [`LICENSE`](LICENSE). The emulator follows beebjit's models
closely and includes B-em's teletext glyphs, both GPL. ROM and disc images
are not part of this repository and are not covered by this licence.
