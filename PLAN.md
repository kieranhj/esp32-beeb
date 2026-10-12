# esp32-beeb plan

A BBC Master 128 emulator for the Waveshare ESP32-S3-Touch-AMOLED-1.8, built to
play the bitshifters demos in `discs/` on the 1.8-inch AMOLED. Accuracy of the
video tricks those demos use is the design goal; the emulator is written for
this chip rather than ported, with beebjit as the reference and test oracle.

Decisions already taken are in `docs/decisions.md`. This file is only what is
left to do, in order. Tick items off and delete phases as they finish.

## Status

The emulator plays the demos on the board. `main/cpu6502.c` is a 65C12
(NMOS mode too) with exact bus sequencing; `main/machine.c` is the Master
(paging, VIAs, 1 MHz stretch, CMOS, 1770 and drives, the 6845 in
`main/crtc.c`, the SN76489 in `main/sn76489.c`); `main/render.c` and
`main/teletext.c` turn the video stream into pixels. On the host it traces
identical to beebjit for 6 M cycles of disc boot and 2 M without, and 16
frame cases (MODE 0 to 7, palette, teletext, five CRTC test discs) are
pixel identical. On the board: core 1 runs the machine paced to real time,
core 0 renders, scales into rotated bands for the panel, renders sound and
polls touch and PWR. All 44 discs are in a flash bundle, chosen with
`tools/beeb.py COM3 boot x.ssd` or by swiping. Kieran has seen the picture
right way up and heard the sound.

Where it stands against the budget (core 1, 120 clocks per emulated cycle;
core 0 load), from the STATS line:

| disc | core 1 | core 0 |
|---|---|---|
| crtc-tmoyos | 85 | 60% |
| bs-wave-runner | 100-114 | 69-86% |
| mmode7 (teletext) | 67 | 80% |
| bs-beebstep, bs-teletextr (loading) | 93-96 | 84-85% |
| bs-twisted | 66-115 (the twister 102, the ending 106-115) | 56-88% |
| bs-sotc, bs-mott (VIA pollers) | 77-82 | 58% |
| bs-patarty | 79-93 | 63% |

Next: bs-twisted's divergence from beebjit (a 1770 control write), then
core 0 on the teletext and loading screens, then the SN76489 write
cycle, then frame cases for the rest of `discs/`.

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
      detection by probing the touch controller, 16-row bands from three DMA
      buffers in internal SRAM. The esp_lcd panel io was dropped: it polls
      every command and drains the DMA queue first, so a band could not be
      queued behind the one in flight. The SPI master driver is used
      directly and a band is two queued transactions.
- [x] Rotation: the CO5300 ignores MADCTL's row/column exchange (rows sent
      448 wide wrapped onto its 368 pixel native rows), so the panel runs
      natively and the scaler writes each band rotated: 16 landscape rows
      are 16 native columns by 448 rows (`display.h`). Seen right way up
      by Kieran.
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
- [x] Core 1 inside the budget on a demo: 148 to 106 clocks per cycle on
      `crtc-tmoyos` (linear fetch runs, hsync, line-start chain and DISPEN
      settling in the CRTC fast path, one catch-up per 1 MHz access, no
      per-cycle syncs during the chain). See decisions, "a demo on the
      board, profiled". Re-measure on the heavier demos once they load.
- [x] Core 0 on a bitmap demo with per-scanline palette writes: 99% to
      80% (colour codes resolved at emit, the run loop specialised, three
      band buffers, the machine waits for ring room instead of lapping).
- [ ] Mode 7: mmode7 runs at 80% of core 0 since the fused teletext run
      (see decisions, "mode 7, a fused run"). Next: the scaler (26% of
      the core on teletext screens, 2.6 K clocks a panel row; adjacent
      rows are adjacent in the rotated band, so a row and its double
      could go out as one 32-bit store), then the run itself (112 clocks
      a character). Re-measure the other teletext discs.
- [ ] Panel path: 44 us of SPI driver time to queue a band (7% of core 0);
      a continuation write (0x3C) would halve the transactions per band.
      The scaler is 14%; a two-pixel lookup would help.
- [x] Core 1 on the VIA pollers and while loading: sotc and mott 139 ->
      111-117 (the quiet VIA path tests the sync point instead of
      recomputing it, and no longer steps through an event tick on the
      access cycle), loading 118-121 -> 104-107 (the 1770's drive timing
      without soft-float doubles). See decisions, "core 1 under budget".
- [x] bs-twisted inside the budget: the twister 171 -> 102, every part
      of the demo 66-115 (CRTC selects through the quiet path, no
      reschedule and a lean catch-up for palette and R12/R13 writes, and
      the core's loop head: one bound and a lazy IRQ history). Every
      other demo measured is 67-96. See decisions, "the core's loop
      head was the cost".
- [ ] bs-twisted diverges from beebjit at cycle 3,531,805 of its boot:
      a 1770 control write while drive 0 spins mid-byte; beebjit applies
      it as if after the drive tick due 13 cycles later. Find why in
      beebjit's timing.c, match it, then add twisted at 100 M as a frame
      case (7800 pixels differ now).
- [ ] An IC32 screen-size change (system VIA, bits 4 and 5) sets the
      CRTC's wrap at once, but the access may have left the CRTC behind:
      catch it up first if the wrap changes. Make a frame case that shows
      it against beebjit before fixing.

## Phase 4: disc and sound

- [x] 1770 FDC (`main/fdc.c`) and discs as pulses (`main/disc.c`): beebjit's
      byte-level model, every command, both drives, FM and MFM. A disc
      boot (shift held, DFS 2.24, a CRTC test disc) traces identical to
      beebjit for 6 M cycles, every one of 2061 NMI edges on the same
      cycle; the CRTC test discs render identically 12 M cycles in. Host
      only so far: `host/frame.exe --disc x.ssd --autoboot`.
- [x] One disc embedded in the image (`EMBED_FILES` in `main/CMakeLists.txt`,
      expanded to pulses in PSRAM at boot, autobooted), the fallback when
      there is no bundle.
- [x] Disc bundle in the `discs` flash partition (`tools/make_disc_bundle.py
      --flash COM3`: a directory of name/offset/size/sides entries then the
      raw images; `main/bundle.c` maps the directory and one image at a
      time, since the data MMU cannot take 12 MB beside PSRAM) and a serial
      console (`main/console.c`, `tools/beeb.py COM3 boot x.ssd`) that
      persists the choice in NVS and resets. All 44 discs are on the board.
- [ ] `.ssd`/`.dsd` on the SD card over SDMMC, through a small sector cache
      in PSRAM.
- [x] SN76489 (`main/sn76489.c`, beebjit's model): the bytes the system
      VIA lands on it go to a queue with their cycle; a task on core 0
      renders the chip 4 ms behind the machine at its own 250 kHz step rate,
      averages eight steps per 31250 Hz sample, blocks DC and writes the
      ES8311 over I2S (`main/sound_esp.c`, esp_codec_dev). The boot beep
      and a demo's tune come out right on the host (`host/sound.exe` writes
      a WAV and reports per half second) and the board plays (32 kHz: the
      ES8311 driver has no 31250 Hz clock entry). `beeb.py COM3 vol 50` sets and keeps the volume.
- [ ] The chip's 8 us write cycle (beebjit's synchronous mode): bytes are
      taken as they land, which a sample player writing the volume
      thousands of times a second may not survive.

## Phase 5: front end

- [ ] Touch menu: list embedded discs and SD discs, tap to boot. Autoboot
      with Shift+BREAK held through reset.
- [x] Input (`main/input.c`): touch holds Space, a long press adds Return,
      a swipe along the picture moves to the next or previous disc in the
      bundle (persisted, then reboot), a PWR short press restarts, a 2 s
      hold turns the sound off or on (not yet tried by hand). Polled at
      50 Hz on core 0, keys handed to the machine through its key queue.
      Tried on the board: taps, swipes and PWR work.
- [ ] Shake (IMU, code in fluidbox `imu.c`) to reset; a per-disc key map.
- [ ] Per-disc notes file in the bundle: machine quirks, which key the
      demo wants, whether it needs sideways RAM in a particular slot.
- [ ] Optional: tiny on-screen keyboard, save state, screenshot to SD.

## Out of scope for v1

NuLA (`bs-bbcnula*.ssd`, `edge-beeb-*nula.ssd`), Model B profile, tape,
Tube, serial, ADFS, hard discs, Music 5000. Each is a possible v2 item.
