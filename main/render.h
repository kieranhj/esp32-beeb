// esp32-beeb: pixel renderer driven by the video stream (vstream.h).
//
// A port of beebjit's render.c beam model (GPL lineage, see decisions.md):
// a canvas of 640 + 2 * border character cells wide and 512 + the same
// high, the beam advancing 8 pixels per 2 MHz tick, hsync returning it to
// the left and down two rasters, vsync to the top, each character row drawn
// on two rasters (deinterlaced). Bitmap modes expand bytes through tables
// built from the ULA palette; teletext goes through the SAA5050 port. With
// the same stream beebjit's video code would have produced, the canvas is
// pixel identical to beebjit's, which is what tools/framediff.py checks.

#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "teletext.h"
#include "vstream.h"

#define RENDER_BORDER_CHARS 4
#define RENDER_WIDTH (640 + RENDER_BORDER_CHARS * 2 * 16)
#define RENDER_HEIGHT (512 + RENDER_BORDER_CHARS * 2 * 16)

enum {
    RENDER_MODE0 = 0, RENDER_MODE1, RENDER_MODE2, RENDER_MODE4, RENDER_MODE5,
    RENDER_MODE7, RENDER_MODE8, RENDER_MODE4_80, RENDER_MODE2_10,
};

struct render {
    uint32_t *buffer;                 // RENDER_WIDTH * RENDER_HEIGHT ARGB
    uint32_t *buffer_end;
    struct teletext teletext;

    uint8_t logical_to_physical[16];
    uint32_t palette[16];
    uint32_t tables_built;            // bit per mode
    uint32_t table_2mhz[4][256][8];   // modes 0, 1, 2, 2_10
    uint32_t table_1mhz[4][256][16];  // modes 4, 5, 8, 4_80
    uint32_t black[8];
    const uint32_t (*cur_2mhz)[8];
    const uint32_t (*cur_1mhz)[16];

    int mode;
    int is_dispen;
    uint32_t row_address;
    int is_clock_2mhz;
    int chars_per_line;
    int is_teletext;
    int is_flash;
    int needs_mode_recalc;
    int selected_blank;               // render the blank function this tick
    int32_t horiz_beam_pos, vert_beam_pos;
    int32_t horiz_window_start, horiz_window_end;
    int32_t vert_window_start, vert_window_end;
    uint32_t *render_pos, *render_pos_row, *render_pos_row_max;
    int32_t cursor_segment_index;
    int cursor_segments[4];
    int is_crt_grayscale_fakeout;
    uint32_t frames;                  // vsyncs seen (flyback count)
};

void render_init(struct render *r, uint32_t *buffer);
void render_reset(struct render *r);   // beebjit's power-on state
// Consumes everything available in the stream.
void render_consume(struct render *r, struct vstream *vs);
// Processes one stream word.
void render_word(struct render *r, uint16_t w);

#endif
