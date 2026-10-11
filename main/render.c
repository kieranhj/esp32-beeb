// esp32-beeb: renderer. See render.h.

#include <string.h>

#include "render.h"

#define BG 0xff000000u

static inline void reset_render_pos(struct render *r)
{
    r->render_pos = r->buffer_end + 1;
    r->render_pos_row = r->buffer_end + 1;
    r->render_pos_row_max = 0;
    int32_t vert = r->vert_beam_pos & ~1;
    if (vert >= r->vert_window_end || vert < r->vert_window_start) {
        return;
    }
    r->render_pos_row = r->buffer + (vert - r->vert_window_start) * RENDER_WIDTH;
    if (r->horiz_beam_pos >= r->horiz_window_end || r->horiz_beam_pos < r->horiz_window_start) {
        return;
    }
    r->render_pos = r->render_pos_row + (r->horiz_beam_pos - r->horiz_window_start);
    r->render_pos_row_max = r->render_pos_row + RENDER_WIDTH - 8;
}

void render_init(struct render *r, uint32_t *buffer)
{
    memset(r, 0, sizeof *r);
    r->buffer = buffer;
    r->buffer_end = buffer + RENDER_WIDTH * RENDER_HEIGHT;
    teletext_init(&r->teletext);
    // beebjit: horizontal and vertical offsets of the standard picture.
    r->horiz_window_start = (15 - RENDER_BORDER_CHARS) * 16;
    r->horiz_window_end = RENDER_WIDTH + r->horiz_window_start;
    r->vert_window_start = (4 - RENDER_BORDER_CHARS) * 16;
    r->vert_window_end = RENDER_HEIGHT + r->vert_window_start;
    r->mode = RENDER_MODE0;
    r->is_clock_2mhz = 1;
    r->chars_per_line = 3;
    r->needs_mode_recalc = 1;
    r->cursor_segment_index = -1;
    for (int i = 0; i < 8; i++) {
        r->black[i] = BG;
    }
    for (uint32_t i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++) {
        buffer[i] = BG;
    }
    render_reset(r);
    r->horiz_beam_pos = 0;
    r->vert_beam_pos = 0;
    reset_render_pos(r);
}

void render_reset(struct render *r)
{
    for (int i = 0; i < 16; i++) {
        r->logical_to_physical[i] = 0;
    }
    r->tables_built = 0;
    static const uint32_t pal[8] = {
        0xff000000, 0xffff0000, 0xff00ff00, 0xffffff00,
        0xff0000ff, 0xffff00ff, 0xff00ffff, 0xffffffff,
    };
    for (int i = 0; i < 8; i++) {
        r->palette[i] = pal[i];
        r->palette[i + 8] = 0;
    }
    // video_ula_power_on_reset: teletext, 1 MHz.
    r->is_clock_2mhz = 0;
    r->chars_per_line = 0;
    r->is_teletext = 1;
    r->needs_mode_recalc = 1;
}

static uint32_t display_color(struct render *r, uint8_t bits)
{
    uint8_t physical = r->logical_to_physical[bits] ^ 7;
    uint32_t pixel = r->palette[physical];
    if (physical & 8) {
        if (pixel == 0) {
            physical &= 7;
            if (r->is_flash) {
                physical ^= 7;
            }
            pixel = r->palette[physical];
        }
    }
    return pixel;
}

static void generate_1mhz(struct render *r, uint32_t (*table)[16], uint32_t num_pixels)
{
    uint32_t stride = 16 / num_pixels;
    for (uint32_t i = 0; i < 256; i++) {
        uint8_t sr = (uint8_t)i;
        uint32_t pixel = 0;
        for (uint32_t j = 0; j < 16; j++) {
            if (j % stride == 0) {
                uint8_t idx = ((sr & 0x02) >> 1) | ((sr & 0x08) >> 2) | ((sr & 0x20) >> 3) | ((sr & 0x80) >> 4);
                pixel = display_color(r, idx);
                sr = (uint8_t)((sr << 1) | 1);
            }
            table[i][j] = pixel;
        }
    }
}

static void generate_2mhz(struct render *r, uint32_t (*table)[8], uint32_t num_pixels)
{
    uint32_t stride = 8 / num_pixels;
    for (uint32_t i = 0; i < 256; i++) {
        uint8_t sr = (uint8_t)i;
        uint32_t pixel = 0;
        for (uint32_t j = 0; j < 8; j++) {
            if (j % stride == 0) {
                uint8_t idx = ((sr & 0x02) >> 1) | ((sr & 0x08) >> 2) | ((sr & 0x20) >> 3) | ((sr & 0x80) >> 4);
                pixel = display_color(r, idx);
                sr = (uint8_t)((sr << 1) | 1);
            }
            table[i][j] = pixel;
        }
    }
}

static void prepare_tables(struct render *r)
{
    int mode = r->mode;
    if (mode == RENDER_MODE7) {
        return;
    }
    if (!(r->tables_built & (1u << mode))) {
        switch (mode) {
        case RENDER_MODE0: generate_2mhz(r, r->table_2mhz[0], 8); break;
        case RENDER_MODE1: generate_2mhz(r, r->table_2mhz[1], 4); break;
        case RENDER_MODE2: generate_2mhz(r, r->table_2mhz[2], 2); break;
        case RENDER_MODE2_10: generate_2mhz(r, r->table_2mhz[3], 1); break;
        case RENDER_MODE4_80: generate_1mhz(r, r->table_1mhz[3], 16); break;
        case RENDER_MODE4: generate_1mhz(r, r->table_1mhz[0], 8); break;
        case RENDER_MODE5: generate_1mhz(r, r->table_1mhz[1], 4); break;
        case RENDER_MODE8: generate_1mhz(r, r->table_1mhz[2], 2); break;
        }
        r->tables_built |= 1u << mode;
    }
    switch (mode) {
    case RENDER_MODE0: r->cur_2mhz = r->table_2mhz[0]; break;
    case RENDER_MODE1: r->cur_2mhz = r->table_2mhz[1]; break;
    case RENDER_MODE2: r->cur_2mhz = r->table_2mhz[2]; break;
    case RENDER_MODE2_10: r->cur_2mhz = r->table_2mhz[3]; break;
    case RENDER_MODE4_80: r->cur_1mhz = r->table_1mhz[3]; break;
    case RENDER_MODE4: r->cur_1mhz = r->table_1mhz[0]; break;
    case RENDER_MODE5: r->cur_1mhz = r->table_1mhz[1]; break;
    case RENDER_MODE8: r->cur_1mhz = r->table_1mhz[2]; break;
    }
}

static inline void select_func(struct render *r)
{
    if (r->mode == RENDER_MODE7) {
        r->selected_blank = 0;
    } else if (r->is_dispen && !(r->row_address & 0x08)) {
        r->selected_blank = 0;
    } else {
        r->selected_blank = 1;
    }
}

static void mode_recalc(struct render *r)
{
    if (!r->needs_mode_recalc) {
        return;
    }
    r->needs_mode_recalc = 0;
    if (r->is_teletext) {
        r->mode = RENDER_MODE7;
    } else if (r->is_clock_2mhz) {
        static const int m[4] = {RENDER_MODE2_10, RENDER_MODE2, RENDER_MODE1, RENDER_MODE0};
        r->mode = m[r->chars_per_line & 3];
    } else {
        static const int m[4] = {RENDER_MODE8, RENDER_MODE5, RENDER_MODE4, RENDER_MODE4_80};
        r->mode = m[r->chars_per_line & 3];
    }
    select_func(r);
    reset_render_pos(r);
}

static void set_mode(struct render *r, int clock_2mhz, int chars_per_line, int is_teletext)
{
    if (clock_2mhz != r->is_clock_2mhz || chars_per_line != r->chars_per_line) {
        r->tables_built = 0;
    }
    r->is_clock_2mhz = clock_2mhz;
    r->chars_per_line = chars_per_line;
    r->is_teletext = is_teletext;
    r->needs_mode_recalc = 1;
}

static void set_flash(struct render *r, int is_flash)
{
    if (r->is_flash == is_flash) {
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (r->logical_to_physical[i] & 8) {
            r->tables_built = 0;
            break;
        }
    }
    r->is_flash = is_flash;
}

static inline void check_cursor(struct render *r, uint32_t *pos, uint32_t *next, int advance)
{
    if (r->cursor_segment_index == -1) {
        return;
    }
    if (r->cursor_segments[r->cursor_segment_index] && pos >= r->render_pos_row) {
        for (int i = 0; i < 8; i++) {
            pos[i] ^= 0x00ffffff;
            if (next) {
                next[i] ^= 0x00ffffff;
            }
        }
    }
    if (!advance) {
        return;
    }
    if (++r->cursor_segment_index == 4) {
        r->cursor_segment_index = -1;
    }
}

static void do_vsync(struct render *r);

static void do_hsync(struct render *r, uint32_t pulse_ticks)
{
    if (r->horiz_beam_pos >= 1536 && r->vert_beam_pos <= 8) {
        r->is_crt_grayscale_fakeout = 1;
    }
    r->horiz_beam_pos = (pulse_ticks & 1) ? -4 : 0;
    r->vert_beam_pos += 2;
    r->cursor_segment_index = -1;
    if (r->vert_beam_pos >= 768) {
        do_vsync(r);
    }
    reset_render_pos(r);
}

static void do_vsync(struct render *r)
{
    if (r->vert_beam_pos < 64) {
        return;
    }
    r->frames++;
    r->vert_beam_pos = 0;
    r->is_crt_grayscale_fakeout = 0;  // the grayscale conversion is not ported
    if (r->horiz_beam_pos >= 512) {
        r->vert_beam_pos = -1;  // even to odd interlace field
    }
    reset_render_pos(r);
}

static void render_tick(struct render *r, uint8_t data, int is_odd_tick)
{
    r->horiz_beam_pos += 8;
    if (r->render_pos <= r->render_pos_row_max) {
        uint32_t *pos = r->render_pos;
        uint32_t *next = pos + RENDER_WIDTH;
        if (r->mode == RENDER_MODE7) {
            teletext_render(&r->teletext, is_odd_tick, pos, next);
            check_cursor(r, pos, next, is_odd_tick);
        } else if (r->is_clock_2mhz) {
            const uint32_t *v = r->selected_blank ? r->black : r->cur_2mhz[data];
            memcpy(pos, v, 32);
            memcpy(next, v, 32);
            check_cursor(r, pos, next, 1);
        } else {
            const uint32_t *v = r->selected_blank ? r->black : (r->cur_1mhz[data] + (is_odd_tick ? 8 : 0));
            memcpy(pos, v, 32);
            memcpy(next, v, 32);
            check_cursor(r, pos, next, is_odd_tick);
        }
        r->render_pos += 8;
    } else if ((r->horiz_beam_pos & ~7) == r->horiz_window_start) {
        reset_render_pos(r);
    } else if (r->horiz_beam_pos >= 1536) {
        do_hsync(r, 0);
    }
}

void render_word(struct render *r, uint16_t w)
{
    if (!(w & VS_EVENT)) {
        uint8_t data = w >> 8;
        int odd = w & VS_ODD;
        int dispen = (w & VS_DISPEN) != 0;
        int ma13 = (w & VS_MA13) != 0;
        if (r->needs_mode_recalc) {
            mode_recalc(r);
        }
        prepare_tables(r);
        // beebjit's rendering tick: DISPEN, then the teletext data pipeline
        // on even ticks, then the render.
        if (r->is_dispen != dispen) {
            r->is_dispen = dispen;
            select_func(r);
        }
        if (!odd) {
            teletext_data(&r->teletext, ma13 ? data : 0, ma13 ? dispen : 0);
        }
        render_tick(r, data, odd);
        return;
    }
    uint8_t payload = w & 0xff;
    switch (w & 0xff00) {
    case VS_HSYNC:
        do_hsync(r, (uint32_t)payload << (r->is_clock_2mhz ? 0 : 1));
        break;
    case VS_VSYNC:
        if (payload) {
            do_vsync(r);
        } else {
            teletext_vsync_changed(&r->teletext, 0);
        }
        break;
    case VS_RA:
        teletext_ra_isv_changed(&r->teletext, payload & 0x1f, (payload & 0x80) != 0);
        r->row_address = payload & 0x1f;
        select_func(r);
        break;
    case VS_CURSOR:
        r->cursor_segment_index = 0;
        break;
    case VS_ULACTRL:
        set_mode(r, (payload >> 4) & 1, (payload >> 2) & 3, (payload >> 1) & 1);
        set_flash(r, payload & 1);
        r->cursor_segments[0] = (payload >> 7) & 1;
        r->cursor_segments[1] = (payload >> 6) & 1;
        r->cursor_segments[2] = (payload >> 5) & 1;
        r->cursor_segments[3] = (payload >> 5) & 1;
        break;
    case VS_ULAPAL:
        r->logical_to_physical[payload >> 4] = payload & 0x0f;
        r->tables_built = 0;
        break;
    default:
        break;
    }
}

void render_consume(struct render *r, struct vstream *vs)
{
    while (vs_available(vs) > 0) {
        render_word(r, vs_get(vs));
    }
}
