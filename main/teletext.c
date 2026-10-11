// esp32-beeb: SAA5050 teletext. See teletext.h.

#include <string.h>

#include "teletext.h"
#include "teletext_glyphs.h"

#ifndef TELETEXT_DATA_ATTR
#define TELETEXT_DATA_ATTR
#endif

// Used for stretching 12 pixels wide into 16.
static const uint8_t k_stretch_data[] = {
    0, 255, 0, 0,  0, 85, 1, 170,  1, 170, 2, 85,  2, 255, 0, 0,
    3, 255, 0, 0,  3, 85, 4, 170,  4, 170, 5, 85,  5, 255, 0, 0,
    6, 255, 0, 0,  6, 85, 7, 170,  7, 170, 8, 85,  8, 255, 0, 0,
    9, 255, 0, 0,  9, 85, 10, 170, 10, 170, 11, 85, 11, 255, 0, 0,
};

static int s_generated;
static TELETEXT_DATA_ATTR uint8_t s_glyphs[96 * 16 * 20];
static TELETEXT_DATA_ATTR uint8_t s_gfx[96 * 16 * 20];
static TELETEXT_DATA_ATTR uint8_t s_sep_gfx[96 * 16 * 20];

static void draw_block(uint8_t *glyph, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            glyph[(y + i) * 6 + x + j] = 1;
        }
    }
}

static void double_up(uint8_t *dest, const uint8_t *src)
{
    for (uint32_t y = 0; y < 10; y++) {
        for (uint32_t x = 0; x < 6; x++) {
            uint8_t *p = dest + y * 24 + x * 2;
            uint8_t v = src[y * 6 + x];
            p[0] = v;
            p[1] = v;
            p[12] = v;
            p[13] = v;
        }
    }
}

static void stretch_12_to_16(uint8_t *dest, const uint8_t *src)
{
    for (uint32_t y = 0; y < 20; y++) {
        const uint8_t *s = k_stretch_data;
        for (uint32_t x = 0; x < 16; x++) {
            uint8_t v = (uint8_t)(src[s[0]] * s[1] + src[s[2]] * s[3]);
            *dest++ = v;
            s += 4;
        }
        src += 12;
    }
}

static void smooth_diagonals(uint8_t *dest, const uint8_t *src)
{
    memcpy(dest, src, 12 * 20);
    for (uint32_t y = 1; y < 19; y++) {
        for (uint32_t x = 1; x < 11; x++) {
            const uint8_t *p = src + y * 12 + x;
            int up = p[-12], down = p[12], left = p[-1], right = p[1];
            int down_left = p[11], up_right = p[-11], down_right = p[13], up_left = p[-13];
            int smooth = (down && left && !down_left) || (up && right && !up_right) ||
                         (down && right && !down_right) || (up && left && !up_left);
            if (smooth) {
                dest[y * 12 + x] = 1;
            }
        }
    }
}

void teletext_generate(void)
{
    if (s_generated) {
        return;
    }
    s_generated = 1;
    for (uint32_t i = 0; i < 96; i++) {
        uint8_t doubled[12 * 20], pretty[12 * 20];
        double_up(doubled, &teletext_characters[i * 60]);
        smooth_diagonals(pretty, doubled);
        stretch_12_to_16(&s_glyphs[i * 320], pretty);
    }
    // 0x40 - 0x5F in graphics modes use the character glyph.
    memcpy(&s_gfx[(0x40 - 0x20) * 320], &s_glyphs[(0x40 - 0x20) * 320], 0x20 * 320);
    memcpy(&s_sep_gfx[(0x40 - 0x20) * 320], &s_glyphs[(0x40 - 0x20) * 320], 0x20 * 320);
    // The graphics glyphs, on a 2x3 grid.
    for (uint32_t i = 0; i < 0x40; i++) {
        uint8_t gfx[6 * 10], sep[6 * 10], doubled[12 * 20];
        memset(gfx, 0, sizeof gfx);
        memset(sep, 0, sizeof sep);
        if (i & 0x01) { draw_block(gfx, 0, 0, 3, 3); draw_block(sep, 1, 0, 2, 2); }
        if (i & 0x02) { draw_block(gfx, 3, 0, 3, 3); draw_block(sep, 4, 0, 2, 2); }
        if (i & 0x04) { draw_block(gfx, 0, 3, 3, 4); draw_block(sep, 1, 3, 2, 3); }
        if (i & 0x08) { draw_block(gfx, 3, 3, 3, 4); draw_block(sep, 4, 3, 2, 3); }
        if (i & 0x10) { draw_block(gfx, 0, 7, 3, 3); draw_block(sep, 1, 7, 2, 2); }
        if (i & 0x20) { draw_block(gfx, 3, 7, 3, 3); draw_block(sep, 4, 7, 2, 2); }
        uint32_t index = i + ((i >= 0x20) ? 0x20 : 0);
        double_up(doubled, gfx);
        stretch_12_to_16(&s_gfx[index * 320], doubled);
        double_up(doubled, sep);
        stretch_12_to_16(&s_sep_gfx[index * 320], doubled);
    }
}

static inline void set_active_characters(struct teletext *t)
{
    if (t->is_graphics_active) {
        t->active_characters = t->is_separated_active ? s_sep_gfx : s_gfx;
    } else {
        t->active_characters = s_glyphs;
    }
}

static inline void reset_scanline_state(struct teletext *t)
{
    t->is_graphics_active = 0;
    t->is_separated_active = 0;
    t->is_concealed = 0;
    t->double_active = 0;
    t->flash_active = 0;
    t->fg_color = t->palette[7];
    t->bg_color = t->palette[0];
    t->is_hold_graphics = 0;
    t->held_character = s_glyphs;  // space
    set_active_characters(t);
}

static inline void advance_scanline(struct teletext *t)
{
    t->scanline++;
    if (t->scanline == 10) {
        t->scanline = 0;
        if (t->second_character_row_of_double) {
            t->second_character_row_of_double = 0;
        } else if (t->had_double_active_this_scanline) {
            t->second_character_row_of_double = 1;
        }
    }
    t->had_double_active_this_scanline = 0;
}

static inline void new_frame_started(struct teletext *t)
{
    t->scanline = 0;
    t->second_character_row_of_double = 0;
    // The flash counter is 6 bits on the silicon.
    t->flash_count = (t->flash_count + 1) & 63;
    t->flash_visible_this_frame = (t->flash_count >= 16);
}

void teletext_init(struct teletext *t)
{
    teletext_generate();
    memset(t, 0, sizeof *t);
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t c = 0;
        if (i & 1) c |= 0x010000;
        if (i & 2) c |= 0x000100;
        if (i & 4) c |= 0x000001;
        t->palette[i] = c;
    }
    t->black = 0xff000000;
    reset_scanline_state(t);
    new_frame_started(t);
}

static inline void handle_control(struct teletext *t, uint8_t c)
{
    switch (c) {
    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
        t->is_graphics_active = 0;
        t->fg_color = t->palette[c];
        if (t->is_concealed) {
            t->is_concealed = 0;
            t->render_fg_color = t->bg_color;  // this character stays concealed
        }
        break;
    case 0x08: t->flash_active = 1; break;
    case 0x09: t->flash_active = 0; break;
    case 0x0c: t->double_active = 0; break;
    case 0x0d:
        t->double_active = 1;
        t->had_double_active_this_scanline = 1;
        break;
    case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
        t->is_graphics_active = 1;
        t->fg_color = t->palette[c & 7];
        if (t->is_concealed) {
            t->is_concealed = 0;
            t->render_fg_color = t->bg_color;
        }
        break;
    case 0x18: t->is_concealed = 1; break;
    case 0x19: t->is_separated_active = 0; break;
    case 0x1a: t->is_separated_active = 1; break;
    case 0x1c: t->bg_color = t->palette[0]; break;
    case 0x1d: t->bg_color = t->fg_color; break;
    case 0x1e: t->is_hold_graphics = 1; break;
    case 0x1f: t->is_hold_graphics = 0; break;
    default: break;  // 0x00 and 0x10: no black select on this part
    }
    set_active_characters(t);
}

static inline void do_data_byte(struct teletext *t, uint8_t data)
{
    int is_hold_graphics = t->is_hold_graphics;
    t->render_fg_color = t->fg_color;
    data &= 0x7f;
    if (data >= 0x20) {
        const uint8_t *ch = t->active_characters + 320 * (data - 0x20);
        t->render_character = ch;
        if (t->is_graphics_active) {
            if (data & 0x20) {
                t->held_character = ch;
            }
        } else {
            t->held_character = s_glyphs;
        }
    } else {
        const uint8_t *held = t->held_character;
        int was_graphics = t->is_graphics_active;
        int was_double = t->double_active;
        handle_control(t, data);
        // Hold on is set-at, hold off is set-after.
        is_hold_graphics |= t->is_hold_graphics;
        if (was_graphics && is_hold_graphics && (t->double_active == was_double)) {
            t->render_character = held;
        } else {
            t->render_character = s_glyphs;
            t->held_character = s_glyphs;
        }
    }
}

void teletext_data(struct teletext *t, uint8_t data, int is_dispen)
{
    uint8_t queue_data = t->data_pipeline[0];
    int pipelined_dispen = t->dispen_pipeline[0];

    // IC37/IC36: bit 6 forced in the pipelined byte while DISPEN is low, so
    // off-screen bytes cannot act as control codes.
    if (!is_dispen) {
        t->data_pipeline[2] |= 0x40;
    }
    do_data_byte(t, queue_data);
    t->data_pipeline[0] = t->data_pipeline[1];
    t->data_pipeline[1] = t->data_pipeline[2];
    t->data_pipeline[2] = data;

    // End of scanline on LOSE (DISPEN) low for at least 2 cycles.
    if (pipelined_dispen == 0 && t->dispen_pipeline[1] == 0 && t->curr_dispen == 1) {
        reset_scanline_state(t);
        advance_scanline(t);
    }
    t->curr_dispen = pipelined_dispen;
    t->dispen_pipeline[0] = t->dispen_pipeline[1];
    t->dispen_pipeline[1] = is_dispen;
}

void teletext_render(struct teletext *t, int is_odd_tick, uint32_t *out, uint32_t *next_out)
{
    const uint8_t *src = t->render_character;
    uint32_t fg = t->render_fg_color;
    uint32_t bg = t->bg_color;

    if (!t->curr_dispen) {
        for (int i = 0; i < 8; i++) {
            if (out) out[i] = t->black;
            if (next_out) next_out[i] = t->black;
        }
        return;
    }
    if ((t->flash_active && !t->flash_visible_this_frame) ||
        (t->second_character_row_of_double && !t->double_active)) {
        src = s_glyphs;
    }
    if (t->is_concealed) {
        src = s_glyphs;
    }
    uint32_t line = t->scanline;
    if (!t->double_active) {
        line *= 2;
    } else if (t->second_character_row_of_double) {
        line += 10;
    }
    int rounded = 0;
    if (!t->is_isv && t->crtc_ra0 && !t->double_active) {
        rounded = 1;
    }
    if (next_out == 0 && t->crtc_ra0 && !t->double_active) {
        rounded = 1;
    }
    if (rounded) {
        line++;
    }
    src += line * 16;
    if (is_odd_tick) {
        src += 8;
    }
    for (int i = 0; i < 8; i++) {
        uint32_t v = src[i];
        out[i] = (v * fg + (255 - v) * bg) | 0xff000000;
    }
    if (!next_out) {
        return;
    }
    if (t->is_isv && !t->double_active) {
        src += 16;
    }
    for (int i = 0; i < 8; i++) {
        uint32_t v = src[i];
        next_out[i] = (v * fg + (255 - v) * bg) | 0xff000000;
    }
}

void teletext_ra_isv_changed(struct teletext *t, uint8_t ra, int is_isv)
{
    t->crtc_ra0 = ra & 1;
    t->is_isv = is_isv;
}

void teletext_vsync_changed(struct teletext *t, int value)
{
    (void)value;
    new_frame_started(t);
}
