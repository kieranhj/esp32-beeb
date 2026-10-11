# esp32-beeb plan

A BBC Master 128 emulator for the Waveshare ESP32-S3-Touch-AMOLED-1.8, built to
play the bitshifters demos in `discs/` on the 1.8-inch AMOLED. Accuracy of the
video tricks those demos use is the design goal; the emulator is written for
this chip rather than ported, with beebjit as the reference and test oracle.

Decisions already taken are in `docs/decisions.md`. This file is only what is
left to do, in order. Tick items off and delete phases as they finish.

## Status

Phase 0 is done: the measured cost is 12 to 17 host clocks per emulated cycle
for the CPU alone and 35 to 41 with a video-shaped per-cycle step, against a
budget of 120 (see `docs/decisions.md`, "Phase 0 results"). The cycle-stepped
design stands. `main/cpu6502.c` is a 65C12 (with an NMOS mode) with exact
bus sequencing that passes the Klaus Dormann tests on the host and the board.
`main/machine.c`
is the Master: paging, VIAs, 1 MHz stretch, CMOS, ACIA/ADC/serial stubs, and
the 6845 state machine in `main/crtc.c` (counters, syncs, address generation,
no pixels yet). On the host, MOS 3.20 boots and the instruction trace matches
beebjit cycle for cycle for the first 10 M cycles (2.77 M instructions),
including every VIA, vsync and ADC interrupt. `main/main.c` is still the
Phase 0 benchmark. Next: the machine on the board with a clocks-per-cycle
measurement, then the pixel side of Phase 2.

## The budget

- 6502 at 2 MHz, host at 240 MHz: **120 host clocks per emulated cycle**.
  Measured in Phase 0: 12 to 17 for the CPU, about 24 more for a video-shaped
  per-cycle step. Target spend from here: 20 CPU, 40 CRTC/ULA, 15 VIAs, the
  rest margin. Re-measure in Phase 3 with core 0 driving the panel.
- Core 1 runs the machine. Core 0 scales, drives the panel, mixes audio, reads
  touch and IMU. Lines cross between them through a scanline ring buffer.
- Hot data in internal SRAM only: 64 KB Master RAM (main, shadow, ANDY,
  HAZEL), the current sideways RAM bank, CPU/CRTC/ULA/VIA state, the scanline
  ring. ROMs read from flash through the data cache via a 16-entry bank
  pointer table. PSRAM holds discs, the mode 7 font and menu assets only.
- The emulator loop and its tables must be placed in IRAM/DRAM (`IRAM_ATTR`,
  `DRAM_ATTR`) and verified with the map file. Phase 0 found no penalty yet
  (the hot set fits the shared 32 KB icache), but core 0 will compete for the
  same caches once the panel runs. Keep the hot working set small.
- Hot state in locals inside the run loop, computed-goto dispatch, 32-bit
  cycle counter: the three things Phase 0 measured as mattering. Details in
  `docs/decisions.md`.

## Phase 1: the machine without a screen

- [x] 65C12 opcode map and bus quirks (see decisions, "the Master's CPU is
      a 65C12"); Klaus Dormann's five tests pass via `sh host/build.sh`.
- [x] Interrupt polling before the penultimate cycle (IRQ line history in
      the core, I flag as of instruction start, RTI uses the pulled flag).
- [x] Master memory map as page tables with two sets for the VDU-driver
      shadow rule; ROMSEL/ANDY, ACCCON/HAZEL/LYNNE.
- [x] Two 6522 VIAs stepped per 1 MHz tick, 1 MHz bus stretch, keyboard
      matrix (nothing pressed yet), IC32, CMOS/RTC reads from defaults.
- [x] Minimal SHEILA decode: ACIA status via a serial ULA model, ADC with
      conversion time and CB1, 1770 idle, CRTC/ULA register stores.
- [x] Trace diff: `host/trace.exe` and `tools/tracediff.py`; see CLAUDE.md
      for the command. Matches beebjit for 1.44 M cycles of boot.
- [x] Host build of the machine (`host/trace.c` includes `machine.c`).
- [ ] The Phase 0 benchmark replaced by the machine in `main/main.c`, with
      the ROM set in flash and the RAM arrays placed in internal SRAM;
      re-measure clocks per cycle with the machine running MOS.
- [ ] Keyboard input path (keys[] matrix is wired, nothing feeds it) and the
      CMOS write side so *CONFIGURE sticks; both can wait for Phase 5.

## Phase 2: video

- [x] 6845 CRTC stepped per 2 MHz tick (`main/crtc.c`, beebjit's state
      machine restated): all registers, R0..R9 mid-frame effects, R12/R13
      latch point, cursor skew, interlace, DISPEN skew, vsync into CA1.
      Verified by the trace diff through boot; the CRTC test discs will
      verify the rest once there are pixels and a frame diff.
- [ ] A frame diff needs a way to get a frame out of beebjit: there is no
      `BEEBJIT_DUMP_FRAME`; the debugger has `frame_buffer_crc32` and
      `render_x/y`, so either compare CRCs or add a dump to our beebjit
      build (`out/beebjit-win` is built from source by our script).
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
