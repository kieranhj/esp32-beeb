// Host harness: boots the Master with the video stream attached, renders it
// with main/render.c, and dumps the canvas in the format beebjit's patched
// "dumpframe" writes (two uint32 width/height, then ARGB words).
//
//   frame <roms-dir> <cycles> <out.bin> [--writem <cycle> <addr> <hexbytes>]
//         [--key <cycle> <row> <col> <down>]...
//
// --writem pokes bytes into RAM when the run reaches <cycle>, the same as
// beebjit's "writem" debugger command. --key presses (down 1) or releases
// (0) a key of the BBC matrix at <cycle>, like beebjit's keydown/keyup;
// tools/frametest.py generates matching sequences for both sides.
//
// Compare with tools/framediff.py against
//   beebjit.exe -master -mode interp -headless -fast -accurate -debug \
//       -frame-cycles 1 -opt video:always-render \
//       -commands "breakat <cycles>;c;dumpframe <file>;q"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPU6502_ORACLE_COMPAT 1
#define CPU6502_TRACE(cpu) trace_line(cpu, 0)
#define CPU6502_TRACE_IRQ(cpu, nmi) trace_line(cpu, (nmi) ? 2 : 1)
struct cpu6502;
static void trace_line(struct cpu6502 *cpu, int kind);
#include "../main/machine.c"

// Optional instruction trace (--trace file), same format as host/trace.c.
static FILE *trace_file;
static const char *dumpram_path;
static void trace_line(struct cpu6502 *cpu, int kind)
{
    if (!trace_file) {
        return;
    }
    char f[9];
    memset(f, ' ', 8);
    f[8] = 0;
    if (cpu->c) f[0] = 'C';
    if (cpu->z) f[1] = 'Z';
    if (cpu->i) f[2] = 'I';
    if (cpu->d) f[3] = 'D';
    f[5] = '1';
    if (cpu->v) f[6] = 'O';
    if (cpu->n) f[7] = 'N';
    fprintf(trace_file, "%s%04X %02X %02X %02X %02X %s %lu\n",
            kind == 0 ? "" : (kind == 1 ? "IRQ " : "NMI "),
            cpu->pc, cpu->a, cpu->x, cpu->y, cpu->s, f, (unsigned long)cpu->cyc);
}
#include "../main/teletext.c"
#include "../main/render.c"

static struct machine machine;
static struct vstream stream;
static struct render render;
static render_px canvas[RENDER_WIDTH * RENDER_HEIGHT];

// Debug: hsyncs per field and the beam position at each vsync raise.
static int debug_fields;
static unsigned field_hsyncs[8], field_count;
static int field_horiz[8], field_vert[8];
static unsigned long long stream_ticks;
static void consume(struct render *r, struct vstream *vs)
{
    while (vs_available(vs) > 0) {
        uint16_t w = vs->ring[vs->tail & (VS_RING - 1)];
        if (!(w & VS_EVENT)) {
            // A run of ticks goes through the renderer's fast path, as on
            // the board, so the frame tests cover it.
            int n = 0;
            while (n < vs_available(vs) && !(vs->ring[(vs->tail + n) & (VS_RING - 1)] & VS_EVENT)) {
                n++;
            }
            stream_ticks += n;
            render_consume_n(r, vs, n);
            continue;
        }
        vs->tail++;
        if (getenv("FRAME_DEBUG") && ((w & 0xff) == VS_ULACTRL)) {
            fprintf(stderr, "stream tick %llu: ULACTRL %02x (teletext=%d)\n", stream_ticks, w >> 8, r->is_teletext);
        }
        if ((w & VS_EVENT) && (w & 0xff) == VS_HSYNC) {
            field_hsyncs[field_count & 7]++;
        }
        if ((w & VS_EVENT) && (w & 0xff) == VS_VSYNC && (w >> 8)) {
            field_horiz[field_count & 7] = r->horiz_beam_pos;
            field_vert[field_count & 7] = r->vert_beam_pos;
            if (getenv("FRAME_DEBUG") && field_count > 260) {
                const struct crtc *c = &machine.crtc;
                fprintf(stderr, "vsync raise: frames=%u odd_frame=%u interlace=%u even_vsync=%u odd_vsync=%u hc=%u vc=%u had_even=%u had_odd=%u r8=%02x\n",
                        c->frames, c->is_odd_frame, c->is_interlace, c->is_even_vsync, c->is_odd_vsync,
                        c->horiz_counter, c->vert_counter, c->had_even_vsync_this_row, c->had_odd_vsync_this_row, c->reg[8]);
            }
            field_count++;
            field_hsyncs[field_count & 7] = 0;
        }
        render_word(r, w);
    }
}

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
    // Events in command line order; both kinds share one list.
    struct { uint64_t cycle; int is_key; unsigned addr; const char *hex; int row, col, down; } pokes[512];
    int npokes = 0;
    for (int i = 4; i < argc; ) {
        if (strcmp(argv[i], "--writem") == 0 && i + 3 < argc && npokes < 512) {
            pokes[npokes].cycle = strtoull(argv[i + 1], NULL, 10);
            pokes[npokes].is_key = 0;
            pokes[npokes].addr = strtoul(argv[i + 2], NULL, 16);
            pokes[npokes].hex = argv[i + 3];
            npokes++;
            i += 4;
        } else if (strcmp(argv[i], "--key") == 0 && i + 4 < argc && npokes < 512) {
            pokes[npokes].cycle = strtoull(argv[i + 1], NULL, 10);
            pokes[npokes].is_key = 1;
            pokes[npokes].row = atoi(argv[i + 2]);
            pokes[npokes].col = atoi(argv[i + 3]);
            pokes[npokes].down = atoi(argv[i + 4]);
            npokes++;
            i += 5;
        } else if (strcmp(argv[i], "--dumpram") == 0 && i + 1 < argc) {
            dumpram_path = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--crtc") == 0 && i + 1 < argc && npokes < 512) {
            // Print the CRTC counters at this cycle (debug).
            pokes[npokes].cycle = strtoull(argv[i + 1], NULL, 10);
            pokes[npokes].is_key = 2;
            npokes++;
            i += 2;
        } else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
            trace_file = fopen(argv[i + 1], "w");
            i += 2;
        } else {
            i++;
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
    render.compat_clear = 1;
    machine_reset(m);
    for (int i = 0; i < npokes; i++) {
        if (pokes[i].is_key == 1) {
            machine_queue_key(m, (uint32_t)pokes[i].cycle, pokes[i].row, pokes[i].col, pokes[i].down);
        }
    }

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
        consume(&render, &stream);
        while (next_poke < npokes && m->cpu.cycles >= pokes[next_poke].cycle) {
            if (pokes[next_poke].is_key == 2) {
                machine_sync(m, m->cpu.cyc);
                consume(&render, &stream);
                fprintf(stderr, "BEAM %llu: x=%ld y=%ld mode=%d\n", (unsigned long long)m->cpu.cycles,
                        (long)render.horiz_beam_pos, (long)render.vert_beam_pos, render.mode);
                const struct crtc *c = &m->crtc;
                fprintf(stderr, "CRTC %llu: c0=%u c4=%u c9=%u ma=%04x frames=%u vadj=%u eom=%u eov=%u eof=%u inva=%u dummy=%u vsync=%u dispen=%u r4hit=%u r9hit=%u\n",
                        (unsigned long long)m->cpu.cycles, c->horiz_counter, c->vert_counter, c->scanline_counter,
                        c->address_counter, c->frames, c->vert_adjust_counter, c->is_end_of_main_latched,
                        c->is_end_of_vert_adjust_latched, c->is_end_of_frame_latched, c->is_in_vert_adjust,
                        c->in_dummy_raster, c->in_vsync, c->display_enable_bits, c->r4_hit, c->r9_hit);
            } else if (pokes[next_poke].is_key) {
                // already queued at start
            } else {
                unsigned addr = pokes[next_poke].addr;
                const char *h = pokes[next_poke].hex;
                while (h[0] && h[1]) {
                    unsigned v;
                    sscanf(h, "%2x", &v);
                    m->ram[addr++ & 0x7fff] = (uint8_t)v;
                    h += 2;
                }
            }
            next_poke++;
        }
    }
    // Flush the peripherals to the exact cycle and render what that produced.
    machine_sync(m, m->cpu.cyc);
    consume(&render, &stream);
    render_flush(&render);

    FILE *f = fopen(argv[3], "wb");
    if (!f) { perror(argv[3]); return 2; }
    uint32_t hdr[2] = {RENDER_WIDTH, RENDER_HEIGHT};
    fwrite(hdr, 4, 2, f);
#ifdef RENDER_INDEXED
    // The board's pixel type; expand so the same diff tools apply.
    for (uint32_t i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++) {
        uint32_t p = canvas[i];
        uint32_t argb = 0xff000000u | ((p & 1) * 0xff0000u) | ((p & 2) ? 0xff00u : 0) | ((p & 4) ? 0xffu : 0);
        fwrite(&argb, 4, 1, f);
    }
#else
    fwrite(canvas, 4, RENDER_WIDTH * RENDER_HEIGHT, f);
#endif
    fclose(f);
    if (trace_file) {
        fclose(trace_file);
    }
    if (dumpram_path) {
        FILE *rf = fopen(dumpram_path, "wb");
        if (rf) {
            fwrite(m->ram, 1, sizeof m->ram, rf);
            fclose(rf);
        }
    }
    if (getenv("FRAME_DEBUG")) {
        for (unsigned i = 0; i < 8; i++) {
            unsigned k = (field_count + 1 + i) & 7;
            fprintf(stderr, "field -%u: hsyncs=%u beam at vsync raise=%d,%d\n", 7 - i, field_hsyncs[k], field_horiz[k], field_vert[k]);
        }
    }
    fprintf(stderr, "ran %llu cycles, %lu frames, pc=%04x, beam %ld,%ld\n",
            (unsigned long long)m->cpu.cycles, (unsigned long)render.frames, m->cpu.pc,
            (long)render.horiz_beam_pos, (long)render.vert_beam_pos);
    return 0;
}
