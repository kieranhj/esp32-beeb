# ROM images

Put the BBC Master 128 MOS 3.20 ROM set in `mos3.20/`. The images are
copyright Acorn Computers and are not in this repository; beebjit and BeebEm
carry the set (beebjit's `roms/mos3.20`, itself from BeebEm 0.6-1). The
firmware build embeds them, so it needs all eight files.

Sideways slot assignment, matching what the beebjit libretro core used and what
MOS 3.20 expects:

| Slot | Image          | Notes                              |
| ---- | -------------- | ---------------------------------- |
| MOS  | `mos.rom`      | 16 KB at 0xC000, with HAZEL/ANDY  |
| 9    | `dfs.rom`      | DFS 2.24, 1770                     |
| A    | `viewsht.rom`  | not needed for demos               |
| B    | `edit.rom`     | not needed for demos               |
| C    | `basic4.rom`   | BASIC 4                            |
| D    | `adfs.rom`     | ADFS, not needed for `.ssd`        |
| E    | `view.rom`     | not needed for demos               |
| F    | `terminal.rom` | not needed for demos               |

Slots 4 to 7 are sideways RAM on a real Master. Demos that use sideways RAM
expect it there.

The ROM images are linked into the firmware as `const` data and read
through the flash cache; they are never copied into internal RAM.
