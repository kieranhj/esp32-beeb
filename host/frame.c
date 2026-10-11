// Host harness: boots the Master with the video stream attached, renders it
// with main/render.c, and dumps the canvas in the format beebjit's patched
// "dumpframe" writes (two uint32 width/height, then ARGB words).
//
//   frame <roms-dir> <cycles> <out.bin> [--writem <cycle> <addr> <hexbytes>]...
//
// --writem pokes bytes into RAM when the run reaches <cycle>, the same as
// beebjit's "writem" debugger command; typing into the MOS keyboard buffer
// at 0x3E0 with the end pointer at 0x2E1 runs commands on both sides.
//
// Compare with tools/framediff.py against
//   beebjit.exe -master -mode interp -headless -fast -accurate -debug \
//       -frame-cycles 1 -opt video:always-render \
//       -commands "breakat <cycles>;c;dumpframe <file>;q"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPU6502_ORACLE_COMPAT 1
#include "../main/machine.c"
#include "../main/teletext.c"
#include "../main/render.c"

static struct machine machine;
static struct vstream stream;
static struct render render;
static uint32_t canvas[RENDER_WIDTH * RENDER_HEIGHT];

static const uint8_t *load_rom(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    uint8_t *buf = malloc(16384);
    if (fread(buf, 1, 16384, f) != 16384) { fprintf(stderr, "%s: short\n", path); exit(2); }
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: frame <roms-dir> <cycles> <out.bin>\n");
        return 2;
    }
    uint64_t cycles = strtoull(argv[2], NULL, 10);
    struct machine *m = &machine;
    struct { uint64_t cycle; unsigned addr; const char *hex; } pokes[16];
    int npokes = 0;
    for (int i = 4; i + 3 < argc; i += 4) {
        if (strcmp(argv[i], "--writem") == 0 && npokes < 16) {
            pokes[npokes].cycle = strtoull(argv[i + 1], NULL, 10);
            pokes[npokes].addr = strtoul(argv[i + 2], NULL, 16);
            pokes[npokes].hex = argv[i + 3];
            npokes++;
        }
    }
    int next_poke = 0;
    machine_init(m);
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) {
        m->swram[i] = calloc(0x4000, 1);
    }
    m->mos = load_rom(argv[1], "mos.rom");
    m->rom[0x9] = load_rom(argv[1], "dfs.rom");
    m->rom[0xa] = load_rom(argv[1], "viewsht.rom");
    m->rom[0xb] = load_rom(argv[1], "edit.rom");
    m->rom[0xc] = load_rom(argv[1], "basic4.rom");
    m->rom[0xd] = load_rom(argv[1], "adfs.rom");
    m->rom[0xe] = load_rom(argv[1], "view.rom");
    m->rom[0xf] = load_rom(argv[1], "terminal.rom");
    m->vs = &stream;
    render_init(&render, canvas);
    machine_reset(m);

    while (m->cpu.cycles < cycles) {
        uint64_t step = cycles - m->cpu.cycles;
        if (next_poke < npokes && pokes[next_poke].cycle > m->cpu.cycles &&
            pokes[next_poke].cycle - m->cpu.cycles < step) {
            step = pokes[next_poke].cycle - m->cpu.cycles;
        }
        if (step > 4096) {
            step = 4096;
        }
        machine_run(m, m->cpu.cycles + step);
        render_consume(&render, &stream);
        while (next_poke < npokes && m->cpu.cycles >= pokes[next_poke].cycle) {
            unsigned addr = pokes[next_poke].addr;
            const char *h = pokes[next_poke].hex;
            while (h[0] && h[1]) {
                unsigned v;
                sscanf(h, "%2x", &v);
                m->ram[addr++ & 0x7fff] = (uint8_t)v;
                h += 2;
            }
            next_poke++;
        }
    }
    // Flush the peripherals to the exact cycle and render what that produced.
    machine_sync(m, m->cpu.cyc);
    render_consume(&render, &stream);

    FILE *f = fopen(argv[3], "wb");
    if (!f) { perror(argv[3]); return 2; }
    uint32_t hdr[2] = {RENDER_WIDTH, RENDER_HEIGHT};
    fwrite(hdr, 4, 2, f);
    fwrite(canvas, 4, RENDER_WIDTH * RENDER_HEIGHT, f);
    fclose(f);
    fprintf(stderr, "ran %llu cycles, %lu frames, pc=%04x, beam %ld,%ld\n",
            (unsigned long long)m->cpu.cycles, (unsigned long)render.frames, m->cpu.pc,
            (long)render.horiz_beam_pos, (long)render.vert_beam_pos);
    return 0;
}
