# esp32-beeb plan

A BBC Master 128 emulator for the Waveshare ESP32-S3-Touch-AMOLED-1.8, built to
play the bitshifters demos in `discs/` on the 1.8-inch AMOLED. Accuracy of the
video tricks those demos use is the design goal; the emulator is written for
this chip rather than ported, with beebjit as the reference and test oracle.

Decisions already taken are in `docs/decisions.md`. This file is only what is
left to do, in order. Tick items off and delete phases as they finish.

## Status

Nothing is implemented. The ESP-IDF skeleton builds and prints heap sizes.
Phase 0 is next.

## The budget

- 6502 at 2 MHz, host at 240 MHz: **120 host clocks per emulated cycle**.
  Target spend: about 40 CPU, about 30 CRTC/ULA, about 10 VIAs, the rest
  margin. Above about 80 clocks measured in Phase 0, the video model changes
  (see Phase 0 fallback) before anything else is written.
- Core 1 runs the machine. Core 0 scales, drives the panel, mixes audio, reads
  touch and IMU. Lines cross between them through a scanline ring buffer.
- Hot data in internal SRAM only: 64 KB Master RAM (main, shadow, ANDY,
  HAZEL), the current sideways RAM bank, CPU/CRTC/ULA/VIA state, the scanline
  ring. ROMs read from flash through the data cache via a 16-entry bank
  pointer table. PSRAM holds discs, the mode 7 font and menu assets only.
- The emulator loop and its tables must be placed in IRAM/DRAM (`IRAM_ATTR`,
  `DRAM_ATTR`) and verified with the map file; an icache miss storm is the
  silent failure mode on this chip.

## Phase 0: measure (1 to 2 days)

Goal: one number, host clocks per emulated 6502 cycle, measured on the board.

- [ ] Minimal 6502 interpreter in `main/cpu6502.c`: documented opcodes only,
      cycle counted per bus access (one function call or inline per cycle,
      the structure Phase 1 will keep), flat 64 KB RAM, no I/O.
- [ ] Run beebjit's timing ROM or a tight synthetic loop for 10 s; print
      cycles/s and clocks per cycle over the serial console.
- [ ] Repeat with a per-cycle hook that does representative CRTC/ULA work
      (increment a character counter, fetch a byte, shift 8 pixels into a
      line buffer) to measure the video tax.
- [ ] Repeat with the loop forced into flash (no `IRAM_ATTR`) to see the
      cache penalty, so the placement rules are grounded in a number.
- [ ] Record all numbers in `docs/decisions.md` under "Phase 0 results".

Fallback if the number is bad: step the video at 1 MHz character clock with
the CPU taking two steps per video step, and only model mid-character palette
changes to the nearest character cell. Document the choice.

## Phase 1: the machine without a screen

- [ ] 6502 complete: all 256 opcodes including undocumented ones, exact
      read/write sequencing per cycle (dummy reads, page-crossing, RMW double
      writes), NMI/IRQ timing, decimal mode.
- [ ] Master memory map: ROMSEL (0xFE30) with ANDY, ACCCON (0xFE34) with
      HAZEL, shadow (Y), VDU-driver shadow select (E/X), TST bit ignored.
      All paging as pointer-table updates, never copies.
- [ ] Two 6522 VIAs: timers 1 and 2 with the real reload/underflow timing,
      PCR/ACR, IFR/IER, keyboard matrix scanning through the system VIA, the
      1 MHz bus stretch on VIA and other 1 MHz peripheral accesses.
- [ ] Minimal 0xFE4x..0xFEFF decode so MOS boots: ACIA, serial ULA, 1770
      stubs returning idle, CMOS/RTC enough for MOS 3.20 not to complain.
- [ ] Trace diff tool in `tools/`: beebjit with `BEEBJIT_DEBUG=1
      BEEBJIT_PRINT=1 -mode interp` prints every instruction; our core
      prints the same format over serial (or in a host build, see below);
      the tool reports the first divergence. Target: identical streams for
      the first million cycles of MOS 3.20 boot.
- [ ] Host build: the core compiles with a desktop C compiler too (no ESP
      headers in `cpu6502.c`, `via.c`, `machine.c`), so trace diffs run on
      the PC in seconds instead of over a 115200 baud link.

## Phase 2: video

- [ ] 6845 CRTC stepped per character clock: all registers, R0..R9 effects
      mid-frame (vertical rupture), R12/R13 at the right latch point, cursor,
      interlace, the skew the ULA applies.
- [ ] Video ULA at the pixel clock: control register, 16-entry palette,
      teletext/graphics select, 2 MHz vs 1 MHz character rate, flash colour,
      mid-line palette writes landing on the right pixel.
- [ ] Scanline output: each completed line is 16-bit pixels at native ULA
      resolution (640 or 320 or 160 wide, plus border), pushed to the ring
      with its line number and a frame-start marker.
- [ ] Frame diff tool: `BEEBJIT_DUMP_FRAME=<path>` on the PC vs our dumped
      frame for the same demo and frame count. Run across the whole
      `discs/` set; the CRTC test discs (`crtc-*.ssd`) are the first targets.
- [ ] Mode 7 teletext: SAA5050 character ROM, double height, graphics,
      hold graphics, flashing. Important but may slip to v2; the demos that
      need it are listed in `docs/decisions.md`.

## Phase 3: presentation

- [ ] Panel bring-up copied from `esp32-fluidbox/fluidbox/main/display.c`:
      CO5300 over QSPI, V2 detection, band DMA with two internal buffers.
- [ ] Rotation to landscape: the panel is 368 wide by 448 tall; the picture
      is 448 by 336 landscape, so bands are emitted as columns of the
      portrait panel. Work out the cheapest transposed write once in Phase 3
      and keep it.
- [ ] Scaler: 640 to 448 horizontal is a 10:7 pattern, 256 to 336 vertical
      is 16:21. Both are fixed repeat/drop tables, no filtering. Colour
      border fills the rest.
- [ ] 50 Hz pacing: the machine runs until vsync, hands the frame over, and
      core 0 presents it at the next panel refresh. Measure end-to-end
      latency and drift.
- [ ] Brightness control and a frame-time overlay toggle for tuning.

## Phase 4: disc and sound

- [ ] 1770 FDC: enough of the command set for DFS 2.24 on `.ssd`/`.dsd`
      (restore, seek, read sector, read address, force interrupt, write
      sector for demos that save state), with per-sector timing that keeps
      loaders that poll the status register happy.
- [ ] Disc source: embedded bundle in the `discs` flash partition (a simple
      directory of name/offset/size entries followed by raw images), plus
      `.ssd`/`.dsd` on the SD card over SDMMC. Sector reads from flash are
      mmap pointer arithmetic; SD reads go through a small sector cache in
      PSRAM.
- [ ] SN76489 at 4 MHz input clock, generated per emulated cycle slice,
      resampled to the ES8311 over I2S through a ring buffer. Audio is
      produced in lockstep with emulation so it never drifts from video.
- [ ] Bundle tool `tools/make_disc_bundle.py`: builds the partition image
      from a list of discs and flashes it with esptool at the partition
      offset.

## Phase 5: front end

- [ ] Touch menu: list embedded discs and SD discs, tap to boot. Autoboot
      with Shift+BREAK held through reset.
- [ ] Input mapping: tap sends Space (configurable per disc), PWR short
      press is BREAK, shake (IMU, code in fluidbox `imu.c`) resets.
- [ ] Per-disc notes file in the bundle: machine quirks, which key the
      demo wants, whether it needs sideways RAM in a particular slot.
- [ ] Optional: tiny on-screen keyboard, save state, screenshot to SD.

## Out of scope for v1

NuLA (`bs-bbcnula*.ssd`, `edge-beeb-*nula.ssd`), Model B profile, tape,
Tube, serial, ADFS, hard discs, Music 5000. Each is a possible v2 item.
