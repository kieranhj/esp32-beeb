# Disc images

Put `.ssd` (single-sided) and `.dsd` (double-sided) disc images here. None are
in this repository: they belong to their authors. The bitshifters demos can be
downloaded from https://bitshifters.github.io/.

- The firmware build embeds one of them as a fallback disc:
  `crtc-tmoyos.ssd` by default, or `idf.py "-DBEEB_DISC=name.ssd" build`.
- `python tools/make_disc_bundle.py --flash COM3` packs every image here
  into the board's `discs` flash partition.
- `tools/frametest.py` and `tools/discsweep.py` compare the emulator with
  beebjit on the images here.
