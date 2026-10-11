# ROM images

`mos3.20/` is the BBC Master 128 MOS 3.20 ROM set, copied unchanged from the
beebjit tree (`rg40xxv/beebjit/roms/mos3.20`, itself taken from BeebEm 0.6-1).
The images are copyright Acorn Computers; they are here for private use with
this emulator and must not be redistributed with the project.

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

The ROM set stays in flash: the emulator maps it with `esp_partition_mmap()` or
links it as `const` data and reads it through the data cache. It is never copied
into SRAM. A Model B set (OS 1.2, BASIC 2, DFS 0.9) lives in
`rg40xxv/beebjit/roms/` if a Model B profile is ever added.
