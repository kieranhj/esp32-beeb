// esp32-beeb: SAA5050 teletext character generator.
//
// A port of beebjit's teletext.c (GPL, see docs/decisions.md on licensing)
// with the same pipeline, control code handling, hold graphics, double
// height, flash and glyph generation (b-em's glyphs, doubled, smoothed and
// stretched 12 to 16 pixels with anti-aliasing), so that frame diffs against
// beebjit are pixel exact. Output is 8 ARGB pixels per call.

#ifndef TELETEXT_H
#define TELETEXT_H

#include <stdint.h>

struct teletext {
    uint8_t data_pipeline[3];
    uint8_t dispen_pipeline[2];
    uint32_t palette[8];
    uint32_t flash_count;
    int flash_visible_this_frame;
    uint32_t scanline;
    const uint8_t *active_characters;
    int is_graphics_active;
    int is_separated_active;
    int is_concealed;
    int double_active;
    int flash_active;
    int had_double_active_this_scanline;
    int second_character_row_of_double;
    uint32_t fg_color;
    uint32_t bg_color;
    int is_hold_graphics;
    const uint8_t *held_character;
    int crtc_ra0;
    int is_isv;
    int curr_dispen;
    const uint8_t *render_character;
    uint32_t render_fg_color;
    uint32_t black;
};

// Generates the glyph tables once (about 92 KB of static data).
void teletext_generate(void);
void teletext_init(struct teletext *t);
void teletext_data(struct teletext *t, uint8_t data, int is_dispen);
void teletext_ra_isv_changed(struct teletext *t, uint8_t ra, int is_isv);
void teletext_vsync_changed(struct teletext *t, int value);
// Renders 8 pixels to out, and to next_out (the following raster) if given.
void teletext_render(struct teletext *t, int is_odd_tick, uint32_t *out, uint32_t *next_out);

#endif
