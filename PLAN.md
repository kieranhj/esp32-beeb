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
is the Master: paging, VIAs, 1 MHz stretch, CMOS, ACIA/ADC/serial stubs,
the 6845 in `main/crtc.c` and the video stream it emits; `main/render.c`
and `main/teletext.c` turn the stream into pixels. On the host the
instruction trace matches beebjit cycle for cycle through 10 M cycles of
boot and through typed MODE changes, and ten frame comparisons (mode 7 boot,
MODE 0 to 6, line drawing, VDU 19) are pixel identical. Discs load: the 1770 and the drives are beebjit's model restated, and
an autobooted CRTC test disc traces identical to beebjit for 6 M cycles and
renders identically 12 M cycles in, for all five test discs. On the board the
picture is on the panel: the machine on core 1 paced to real time, the
renderer and scaler on core 0, at about 80% of core 0 in bitmap modes and
at the limit in mode 7; the discs are not on the board yet. The panel orientation (mirror bits, V2 gap axis)
has not been seen by a person yet: boot it and read the mode 7 banner, then
fix `DISPLAY_MIRROR_X/Y` in `main/display.h` and `DISPLAY_GAP_X/Y` in
`main/display.c` if it is mirrored, upside down or shifted by 16 pixels.
Next: Phase 4, the 1770 and the discs, which the CRTC test discs need.

## The budget

- 6502 at 2 MHz, host at 240 MHz: **120 host clocks per emulated cycle**.
  Measured with the whole machine at the BASIC prompt: 95 (core 32, CRTC 37,
  VIAs 8, catch-up and I/O paths 18); see decisions "event-driven catch-up".
  Re-measure in Phase 3 with core 0 driving the panel, and with a demo
  running rather than the MOS idle loop.
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
  cycle counter, no call per cycle (the core counts, the machine catches up
  at events), peripherals advanced by subtraction between events: what the
  measurements found to matter. Details in `docs/decisions.md`.

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
- [x] The machine in `main/main.c` with the ROM set in flash, RAM in
      internal SRAM (sideways RAM in PSRAM for now), measured at 95 clocks
      per cycle. `main.c` is still a benchmark harness with profile masks
      (`machine_profile_skip`) and MACHINE_STATS counters.
- [ ] Keyboard input path (keys[] matrix is wired, nothing feeds it) and the
      CMOS write side so *CONFIGURE sticks; both can wait for Phase 5.

## Phase 2: video

- [x] 6845 CRTC stepped per 2 MHz tick (`main/crtc.c`, beebjit's state
      machine restated): all registers, R0..R9 mid-frame effects, R12/R13
      latch point, cursor skew, interlace, DISPEN skew, vsync into CA1.
      Verified by the trace diff through boot; the CRTC test discs will
      verify the rest once there are pixels and a frame diff.
- [x] Video stream (`main/vstream.h`): one 16-bit word per 2 MHz tick from
      the CRTC fast path plus tagged events (hsync, vsync, RA, cursor, ULA
      control and palette writes, framing change), in beebjit's render call
      order. This is what crosses to core 0 on the board.
- [x] Renderer (`main/render.c`): beebjit's beam model and mode tables
      ported; teletext (`main/teletext.c`): beebjit's SAA5050 port with
      b-em's glyphs. Pixel identical to beebjit (`tools/frametest.py`):
      mode 7 boot, MODE 0 to 6, a 16-colour line drawing, VDU 19.
- [x] Frame diff tooling: `tools/build_beebjit_win.sh` patches a
      `dumpframe` command into beebjit; `host/frame.exe` renders our stream
      to beebjit's 768x640 canvas; `tools/framediff.py` and `framecrop.py`
      compare and show; `tools/frametest.py` types commands into both
      through the keyboard matrix at matching cycles.
- [x] Frame diffs on the five CRTC test discs (`crtc-*.ssd`), autobooted,
      12 M cycles in: all pixel identical (`tools/frametest.py` cases
      tmoyos, beebnyan, infinity, nasty, sugarsmash). Found and fixed the
      display reading LYNNE when ACCCON X was set with D clear.
- [ ] The rest of `discs/` as frame cases, a few frames into each demo;
      and mid-line palette writes shown on a demo that does them.
- [ ] A byte written by the CPU on the cycle the CRTC fetches it: one MODE 2
      pixel differed in a frame dumped mid-DRAW (`lines2` now settles
      first). Fetch-before-write or after, which beebjit does the other
      way; find out which and match it.

## Phase 3: presentation

- [x] Panel bring-up (`main/display.c`): CO5300 over QSPI at 80 MHz, V2
      detection by probing the touch controller, 16-row bands from two DMA
      buffers in internal SRAM. The esp_lcd panel io was dropped: it polls
      every command and drains the DMA queue first, so a band could not be
      queued behind the one in flight. The SPI master driver is used
      directly and a band is two queued transactions.
- [x] Rotation: the panel's own row/column exchange (MADCTL), so bands are
      landscape rows and no transpose is needed. Mirror bits and the V2 gap
      axis are defines in `display.h` and `display.c`; they have not been
      seen on the panel yet (see Status).
- [x] Scaler (`main/video_esp.c`): 640 to 448 keeps 7 pixels in 10, 256
      scanlines to 336 rows doubles 5 in 16 (teletext's second raster
      supplies the doubled row), straight from the renderer's raster line
      into the band buffer. No frame buffer; a band goes out when the beam
      leaves it, rows the beam never reaches are black.
- [x] 50 Hz pacing: core 1 runs the machine to wall clock in slices of at
      most a frame, 2 ms ahead then sleeps; core 0 drains the stream. The
      panel trails the emulated beam by one band (about 1 ms). Drift is
      nil by construction (the machine is slaved to the timer); the
      measured costs are in `docs/decisions.md`.
- [x] Brightness control (`display_set_brightness`).
- [ ] Frame-time overlay toggle: needs an input (Phase 5); the serial
      STATS line serves for tuning until then.
- [ ] Mode 7 is at the limit of core 0 (about 85% rendering plus 15% panel
      path) and drops frames at the BASIC prompt; bitmap modes run at about
      80%. Levers, in order: a continuation write (0x3C) to halve the
      transactions per band, a two-pixel lookup in the row scaler, a
      character-row teletext renderer.
- [ ] Core 1 runs at 106 clocks per cycle at the prompt with the stream on
      (96 without), 118 to 128 while BASIC draws lines: the display write
      window syncs the machine on every screen write. Over budget for
      heavy screen writers; the sync needs to get cheaper or rarer.

## Phase 4: disc and sound

- [x] 1770 FDC (`main/fdc.c`) and discs as pulses (`main/disc.c`): beebjit's
      byte-level model, every command, both drives, FM and MFM. A disc
      boot (shift held, DFS 2.24, a CRTC test disc) traces identical to
      beebjit for 6 M cycles, every one of 2061 NMI edges on the same
      cycle; the CRTC test discs render identically 12 M cycles in. Host
      only so far: `host/frame.exe --disc x.ssd --autoboot`.
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
