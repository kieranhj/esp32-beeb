// esp32-beeb: host benchmark of the renderer over a captured video stream
// (one hex word per line, as frame.exe's FRAME_DUMP_TICKS prints after
// `awk '{print $2}'`), indexed pixels and a counting emitter. Prints the
// words through each path, the emits and the time per pass.
//   zig cc -O2 -std=c11 -DRENDER_INDEXED -o host/benchs.exe host/benchs.c
//   host/benchs.exe out/rvi_stream.txt
#include <stdio.h>
#include <stdlib.h>
#include <x86intrin.h>

static unsigned emits, emit_px;
static void count_emit(void *r, unsigned first, unsigned count, int vert) { (void)r; (void)first; (void)vert; emits++; emit_px += count; }
#define RENDER_EMIT(r, first, count, vert) count_emit(r, first, count, vert)
#define RENDER_CYCLES() ((uint32_t)__rdtsc())
#include "../main/render.c"
#include "../main/teletext.c"

static struct render render;
static struct vstream stream;

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: benchs <stream.txt> [passes]\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    static uint16_t words[1 << 22];
    uint32_t nw = 0;
    unsigned v;
    while (nw < (1u << 22) && fscanf(f, "%x", &v) == 1) {
        words[nw++] = (uint16_t)v;
    }
    fclose(f);
    int passes = argc > 2 ? atoi(argv[2]) : 20;
    struct render *r = &render;
    struct vstream *vs = &stream;
    render_init(r, NULL);
    // The capture starts mid-mode: a 2 MHz bitmap mode until its own ULA
    // control event says otherwise.
    vs_put(vs, VS_EV(VS_ULACTRL, 0xf4));
    render_consume(r, vs);
    uint64_t best = ~0ull;
    for (int p = 0; p < passes; p++) {
        emits = emit_px = 0;
        r->stat_fast_words = r->stat_slow_words = 0;
        for (int i = 0; i < 16; i++) {
            r->stat_ev_n[i] = r->stat_ev_clk[i] = 0;
        }
        uint64_t c0 = __rdtsc();
        for (uint32_t i = 0; i < nw;) {
            uint32_t k = nw - i < VS_RING / 2 ? nw - i : VS_RING / 2;
            for (uint32_t j = 0; j < k; j++) {
                vs_put(vs, words[i + j]);
            }
            render_consume(r, vs);
            i += k;
        }
        uint64_t dc = __rdtsc() - c0;
        if (dc < best) {
            best = dc;
        }
    }
    printf("%u words: %u fast, %u slow; %u emits, %u px; best pass %.2f Mtsc\n", nw, r->stat_fast_words,
           r->stat_slow_words, emits, emit_px, best / 1e6);
    static const char *names[16] = {"tick", "hsync", "vsync", "ra", "cursor", "ulactrl", "ulapal", "frame",
                                    "framing", "skip"};
    for (int i = 0; i < 16; i++) {
        if (r->stat_ev_n[i]) {
            printf("  slow %s: %u x %u tsc\n", names[i] ? names[i] : "?", r->stat_ev_n[i], r->stat_ev_clk[i] / r->stat_ev_n[i]);
        }
    }
    return 0;
}
