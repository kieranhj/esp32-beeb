// esp32-beeb: host benchmark of the renderer over a synthetic MODE 2 stream
// (the same one main/video_esp.c times on the board), with path counts.
//   zig cc -O2 -std=c11 -DRENDER_INDEXED -o host/benchr.exe host/benchr.c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static unsigned emits, emit_px;
static void count_emit(void *r, unsigned first, unsigned count, int vert) { (void)r; (void)first; (void)vert; emits++; emit_px += count; }
#define RENDER_EMIT(r, first, count, vert) count_emit(r, first, count, vert)
#include "../main/render.c"
#include "../main/teletext.c"

static struct render render;
static struct vstream stream;

int main(void)
{
    struct render *r = &render;
    struct vstream *vs = &stream;
    render_init(r, NULL);
    vs_put(vs, VS_EV(VS_ULACTRL, 0xf4));
    uint32_t n = 0;
    while (vs->head + 8 * 130 <= VS_RING) {
        for (int line = 0; line < 8; line++) {
            vs_put(vs, VS_EV(VS_RA, line));
            for (int t = 0; t < 128; t++) {
                uint8_t data = (uint8_t)(t * 7 + line * 3 + n);
                vs_put(vs, (uint16_t)((data << 8) | (t & 1) | (t < 80 ? VS_DISPEN : 0)));
            }
            vs_put(vs, VS_EV(VS_HSYNC, 8));
        }
        n++;
    }
    uint32_t words = vs->head;
    for (int i = 0; i < 30; i++) {
        render_consume_n(r, vs, 1);
        printf("after word %d (%04x): mode %d tt %d 2mhz %d cpl %d recalc %d pos %d horiz %d\n", i, vs->ring[i], r->mode,
               r->is_teletext, r->is_clock_2mhz, r->chars_per_line, r->needs_mode_recalc, r->render_pos, r->horiz_beam_pos);
    }
    render_init(r, NULL);
    for (int rep = 0; rep < 3; rep++) {
        vs->tail = 0;
        r->stat_fast_words = r->stat_slow_words = 0;
        clock_t c0 = clock();
        render_consume_n(r, vs, (int)words);
        double ms = (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
        printf("emits %u px %u ", emits, emit_px); emits = emit_px = 0;
        printf("%u words: %u fast, %u slow, %.2f ms, mode %d pos %d recalc %d cursor %d\n",
               words, r->stat_fast_words, r->stat_slow_words, ms, r->mode, r->render_pos,
               r->needs_mode_recalc, r->cursor_segment_index);
    }
    return 0;
}
