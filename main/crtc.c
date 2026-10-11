// esp32-beeb: 6845 CRTC. See crtc.h. Included into the machine's translation
// unit after the hook macros are defined; crtc_tick() is static inline there.

#include "crtc.h"

#ifndef CRTC_API
#define CRTC_API
#endif

#ifndef CRTC_FETCH
#define CRTC_FETCH(c, ma, ra, odd) ((void)(ma), (void)(ra), (void)(odd), (uint8_t)0)
#endif
#ifndef CRTC_RENDER
#define CRTC_RENDER(c, data, dispen, odd, ma13) ((void)0)
#endif
#ifndef CRTC_HSYNC
#define CRTC_HSYNC(c, width) ((void)0)
#endif
#ifndef CRTC_VSYNC
#define CRTC_VSYNC(c, level) ((void)0)
#endif
#ifndef CRTC_CURSOR
#define CRTC_CURSOR(c) ((void)0)
#endif
#ifndef CRTC_TELETEXT
#define CRTC_TELETEXT(c, data, dispen) ((void)0)
#endif
#ifndef CRTC_RA
#define CRTC_RA(c, ra, isv) ((void)0)
#endif

#define CRTC_INLINE static inline __attribute__((always_inline))

enum { DISPEN_HORIZ = 1, DISPEN_VERT = 2, DISPEN_ALL = 3 };

static CRTC_API void crtc_update_cursor_disabled(struct crtc *c)
{
    c->cursor_disabled = ((c->reg[CRTC_R10_CSTART] & 0x60) == 0x20) ||
                         ((c->reg[CRTC_R8_INTERLACE] & 0xc0) == 0xc0);
}

CRTC_INLINE void crtc_update_vsync(struct crtc *c)
{
    int level = (c->is_interlace && !c->is_odd_frame) ? c->is_even_vsync : c->is_odd_vsync;
    if (level == c->in_vsync) {
        return;
    }
    c->in_vsync = level;
    c->vsync_changed = 1;
    CRTC_VSYNC(c, level);
}

static CRTC_API void crtc_start_new_frame(struct crtc *c)
{
    c->scanline_counter = 0;
    c->vert_counter = 0;
    c->vert_adjust_counter = 0;
    c->had_odd_vsync_this_row = 0;
    c->had_even_vsync_this_row = 0;
    c->is_in_vert_adjust = 0;
    c->in_dummy_raster = 0;
    c->is_odd_frame = c->frames & 1;
    c->has_hit_cursor_line_start = 0;
    c->has_hit_cursor_line_end = 0;
    c->is_end_of_main_latched = 0;
    c->is_end_of_vert_adjust_latched = 0;
    c->is_end_of_frame_latched = 0;
    c->is_first_frame_scanline = 1;
    c->display_enable_bits |= DISPEN_VERT;
    c->address_counter = (c->reg[CRTC_R12_MAH] << 8) | c->reg[CRTC_R13_MAL];
    c->address_counter_saved = c->address_counter;
}

// beebjit's check_r6 / check_r7 tail: run at the start of every scanline and
// once at reset, and again after an R7 write (which can start a vsync at
// once on the Hitachi part).
static CRTC_API void crtc_line_start_checks(struct crtc *c, int from_r6)
{
    if (from_r6) {
        int r6_hit = (c->vert_counter == c->reg[CRTC_R6_VDISP]);
        if (r6_hit && (c->display_enable_bits & DISPEN_VERT) && !c->is_first_frame_scanline) {
            c->display_enable_bits &= ~DISPEN_VERT;
            c->frames++;  // the Hitachi counts frames on R6 hit
        }
    }
    int r7_hit = (c->vert_counter == c->reg[CRTC_R7_VSYNC]);
    if (r7_hit && !c->had_odd_vsync_this_row) {
        c->had_odd_vsync_this_row = 1;
        c->is_odd_vsync = 1;
        if (!c->in_vsync) {
            c->vsync_scanline_counter = c->vsync_width;
        }
        crtc_update_vsync(c);
    }
    {
        uint8_t ra = c->scanline_counter;
        if (c->is_isv) {
            ra |= c->is_odd_frame;
        }
        if (c->scanline_counter == c->cursor_start_line) {
            c->has_hit_cursor_line_start = 1;
        }
        CRTC_RA(c, ra, c->is_isv);
    }
    c->r4_hit = (c->vert_counter == c->reg[CRTC_R4_VTOTAL]);
    c->r5_hit = (c->vert_adjust_counter == c->reg[CRTC_R5_VADJ]);
    c->r9_hit = (c->scanline_counter == (c->reg[CRTC_R9_MAXRA] & c->scanline_mask));
}

CRTC_INLINE uint8_t crtc_rendering_tick(struct crtc *c, uint8_t horiz_counter,
                                        int r0_hit, int r9_hit, int is_odd_tick)
{
    int r1_hit = (horiz_counter == c->reg[CRTC_R1_HDISP]);
    if (r1_hit && r9_hit) {
        c->address_counter_saved = c->address_counter;
    }
    if (r1_hit || r0_hit) {
        c->display_enable_bits &= ~DISPEN_HORIZ;
    }

    if (c->in_hsync) {
        if (--c->hsync_tick_counter == 0) {
            c->in_hsync = 0;
        }
    } else if (horiz_counter == c->reg[CRTC_R2_HSYNC] && c->hsync_width > 0) {
        CRTC_HSYNC(c, c->hsync_width);
        c->in_hsync = 1;
        c->hsync_tick_counter = c->hsync_width;
    }

    // DISPEN history for the R8 skew.
    c->dispen_shifts[2] = c->dispen_shifts[1];
    c->dispen_shifts[1] = c->dispen_shifts[0];
    c->dispen_shifts[0] = (c->display_enable_bits == DISPEN_ALL);
    int dispen = c->dispen_shifts[c->skew_dispen_index];

    if (!c->cursor_disabled) {
        uint16_t cursor_addr = (c->reg[CRTC_R14_CAH] << 8) | c->reg[CRTC_R15_CAL];
        if (c->address_counter == cursor_addr && c->has_hit_cursor_line_start &&
            !c->has_hit_cursor_line_end &&
            (!c->cursor_flashing || (c->frames & c->cursor_flash_mask))) {
            c->cursor_skew_counter = c->cursor_skew;
        }
        if (c->cursor_skew_counter >= 0) {
            c->cursor_skew_counter--;
            if (c->cursor_skew_counter == -1 && c->dispen_shifts[c->cursor_skew]) {
                CRTC_CURSOR(c);
            }
        }
    }

    uint8_t ra = c->scanline_counter;
    if (c->is_isv) {
        ra |= c->is_odd_frame;
    }
    uint16_t ma = c->address_counter;
    uint8_t data = CRTC_FETCH(c, ma, ra, is_odd_tick);
    c->data_ma13 = (ma >> 13) & 1;
    if (!is_odd_tick) {
        // The IC15 latch only passes data and DISPEN to the teletext chip
        // under MODE 7 style (MA13 set) addressing.
        if (ma & 0x2000) {
            CRTC_TELETEXT(c, data, dispen);
        } else {
            CRTC_TELETEXT(c, 0, 0);
        }
    }
    c->address_counter = (ma + 1) & 0x3fff;
    (void)dispen;
    c->data_byte = data;
    return data;
}

// One 2 MHz system tick. `tick` is the index of the tick being processed.
CRTC_INLINE void crtc_tick(struct crtc *c, uint32_t tick)
{
    int is_odd_tick = tick & 1;
    int is_1mhz = !c->ula_clock_fast;
    uint8_t r0 = c->reg[CRTC_R0_HTOTAL];
    uint8_t horiz_counter = c->horiz_counter;
    int r0_hit = (horiz_counter == r0);

    if (!is_1mhz || !is_odd_tick) {
        c->data_byte = crtc_rendering_tick(c, horiz_counter, r0_hit, c->r9_hit, is_odd_tick);
    }
    CRTC_RENDER(c, c->data_byte, c->dispen_shifts[c->skew_dispen_index], is_odd_tick, c->data_ma13);

    if (is_1mhz && !is_odd_tick) {
        return;
    }

    uint8_t checks = c->start_of_line_state_checks;
    if (checks) {
        if (checks & 8) {
            if (c->is_end_of_vert_adjust_latched) {
                c->is_end_of_frame_latched = 1;
            }
            checks &= ~8;
        }
        if (checks & 4) {
            if (c->is_end_of_main_latched) {
                if (c->r5_hit) {
                    c->is_end_of_vert_adjust_latched = 1;
                } else if (!c->is_in_vert_adjust && !c->in_dummy_raster) {
                    c->is_vert_adjust_pending = 1;
                }
            }
            checks &= ~4;
            checks |= 8;
        }
        if (checks & 2) {
            if (c->r4_hit && c->r9_hit) {
                c->is_end_of_main_latched = 1;
            }
            checks &= ~2;
            checks |= 4;
        }
        if (checks & 1) {
            checks &= ~1;
            checks |= 2;
        }
    }

    horiz_counter++;  // wraps at 256

    if ((c->vert_counter == c->reg[CRTC_R7_VSYNC]) || c->is_even_vsync) {
        if (horiz_counter == c->half_r0) {
            if (!c->had_even_vsync_this_row) {
                c->had_even_vsync_this_row = 1;
                c->is_even_vsync = 1;
            } else if (c->vsync_scanline_counter == 0) {
                c->is_even_vsync = 0;
            }
            crtc_update_vsync(c);
        }
    }

    if (!r0_hit) {
        c->horiz_counter = horiz_counter;
        c->start_of_line_state_checks = checks;
        return;
    }

    // End of horizontal line.
    horiz_counter = 0;
    c->display_enable_bits |= DISPEN_HORIZ;
    checks |= 1;

    c->is_first_frame_scanline = 0;
    if (c->scanline_counter == c->reg[CRTC_R11_CEND]) {
        c->has_hit_cursor_line_end = 1;
    }

    if (c->in_vsync) {
        if (c->vsync_scanline_counter > 0) {
            c->vsync_scanline_counter--;
        }
        if (c->vsync_scanline_counter == 0) {
            c->is_odd_vsync = 0;
            crtc_update_vsync(c);
        }
    }

    if (r0 != 0) {
        c->scanline_counter = (c->scanline_counter + c->scanline_stride) & c->scanline_mask;
    }
    c->address_counter = c->address_counter_saved;

    if (c->r9_hit && !c->is_in_vert_adjust) {
        c->has_hit_cursor_line_start = 0;
        c->has_hit_cursor_line_end = 0;
        c->had_odd_vsync_this_row = 0;
        c->had_even_vsync_this_row = 0;
        c->scanline_counter = 0;
        c->vert_counter = (c->vert_counter + 1) & 0x7f;
    }

    c->horiz_counter = horiz_counter;
    c->start_of_line_state_checks = checks;

    if (c->in_dummy_raster) {
        crtc_start_new_frame(c);
        crtc_line_start_checks(c, 0);
        return;
    } else if (c->is_vert_adjust_pending) {
        c->is_vert_adjust_pending = 0;
        c->is_in_vert_adjust = 1;
    } else if (c->is_end_of_frame_latched) {
        if (c->is_interlace && (c->frames & 1)) {
            c->in_dummy_raster = 1;
        } else {
            crtc_start_new_frame(c);
            crtc_line_start_checks(c, 0);
            return;
        }
    }

    if (c->is_in_vert_adjust) {
        c->vert_adjust_counter = (c->vert_adjust_counter + 1) & 0x1f;
    }

    crtc_line_start_checks(c, 1);
}

// Ticks until the next tick at which this CRTC could change the vsync
// level (or needs its per-line bookkeeping): the next end of line, or the
// half-R0 point in a row that is at or in vsync. Register writes reset the
// estimate through the machine. Never less than 1.
CRTC_INLINE uint32_t crtc_ticks_to_check(const struct crtc *c)
{
    uint32_t hc = c->horiz_counter;
    uint32_t r0 = c->reg[CRTC_R0_HTOTAL];
    uint32_t to_eol = (hc <= r0) ? (r0 - hc + 1) : (256 - hc + r0 + 1);
    uint32_t t = to_eol;
    if ((c->vert_counter == c->reg[CRTC_R7_VSYNC]) || c->is_even_vsync) {
        uint32_t half = c->half_r0;
        uint32_t to_half = (hc < half) ? (half - hc) : (256 - hc + half);
        if (to_half < t) {
            t = to_half;
        }
    }
    if (c->start_of_line_state_checks) {
        t = 1;  // the start-of-line chain is running
    }
    // 1 MHz mode: counters step every other system tick.
    if (!c->ula_clock_fast) {
        t *= 2;
    }
    return t ? t : 1;
}

// How many system ticks from `tick` can run the fast path: no counter hit
// (R0, R1, R2, the half-R0 vsync point), no start-of-line chain, no hsync
// in progress, no cursor activity, and a settled DISPEN pipeline. Zero
// means the next tick must take the exact path.
CRTC_INLINE uint32_t crtc_fast_ticks(const struct crtc *c, uint32_t tick)
{
    if (c->start_of_line_state_checks || c->in_hsync || c->cursor_skew_counter >= 0) {
        return 0;
    }
    uint8_t dispen = (c->display_enable_bits == DISPEN_ALL);
    if (c->dispen_shifts[0] != dispen || c->dispen_shifts[1] != dispen ||
        c->dispen_shifts[2] != dispen) {
        return 0;
    }
    uint32_t hc = c->horiz_counter;
    uint32_t r0 = c->reg[CRTC_R0_HTOTAL];
    uint32_t l = (hc <= r0) ? (r0 - hc) : (256 - hc + r0);
    uint32_t r1 = c->reg[CRTC_R1_HDISP];
    if (hc <= r1 && r1 - hc < l) {
        l = r1 - hc;
    }
    uint32_t r2 = c->reg[CRTC_R2_HSYNC];
    if (hc <= r2 && r2 - hc < l && c->hsync_width > 0) {
        l = r2 - hc;
    }
    if ((c->vert_counter == c->reg[CRTC_R7_VSYNC]) || c->is_even_vsync) {
        uint32_t half = c->half_r0;
        if (hc + 1 <= half) {
            uint32_t d = half - 1 - hc;
            if (d < l) {
                l = d;
            }
        }
    }
    if (!c->cursor_disabled && c->has_hit_cursor_line_start && !c->has_hit_cursor_line_end) {
        uint32_t cursor = (c->reg[CRTC_R14_CAH] << 8) | c->reg[CRTC_R15_CAL];
        uint32_t d = (cursor - c->address_counter) & 0x3fff;
        if (d < l) {
            l = d;
        }
    }
    if (c->ula_clock_fast) {
        return l;
    }
    // 1 MHz: an even tick fetches, the odd tick after it counts.
    return l ? (2 * l - (tick & 1)) : 0;
}

// The fast path: `n` ticks that only fetch, render and count.
CRTC_INLINE void crtc_fast(struct crtc *c, uint32_t tick, uint32_t n)
{
    uint8_t ra = c->scanline_counter;
    if (c->is_isv) {
        ra |= c->is_odd_frame;
    }
    int dispen = c->dispen_shifts[c->skew_dispen_index];
    uint16_t ma = c->address_counter;
    uint8_t data = c->data_byte;
    uint8_t ma13 = c->data_ma13;
    uint8_t hc = c->horiz_counter;
    int fast = c->ula_clock_fast;
    for (uint32_t i = 0; i < n; i++, tick++) {
        int odd = tick & 1;
        if (fast || !odd) {
            data = CRTC_FETCH(c, ma, ra, odd);
            ma13 = (ma >> 13) & 1;
            if (!odd) {
                CRTC_TELETEXT(c, ma13 ? data : 0, ma13 ? dispen : 0);
            }
            ma = (ma + 1) & 0x3fff;
        }
        CRTC_RENDER(c, data, dispen, odd, ma13);
        if (fast || odd) {
            hc++;
        }
    }
    c->address_counter = ma;
    c->data_byte = data;
    c->data_ma13 = ma13;
    c->horiz_counter = hc;
}

// Runs up to n ticks starting at tick index `first`, stopping after a tick
// that changed the vsync level so the caller can record the IRQ change.
// Returns the number of ticks run (at least 1 when n > 0).
CRTC_INLINE uint32_t crtc_run(struct crtc *c, uint32_t first, uint32_t n)
{
    uint32_t i = 0;
    c->vsync_changed = 0;
    while (i < n) {
        uint32_t fast = crtc_fast_ticks(c, first + i);
        if (fast > n - i) {
            fast = n - i;
        }
        if (fast) {
            crtc_fast(c, first + i, fast);
            i += fast;
#ifdef MACHINE_STATS
            c->stat_fast += fast;
#endif
            continue;
        }
        crtc_tick(c, first + i);
        i++;
#ifdef MACHINE_STATS
        c->stat_slow++;
#endif
        if (c->vsync_changed) {
            break;
        }
    }
    return i;
}

CRTC_API void crtc_set_ula(struct crtc *c, uint8_t ula_control)
{
    c->ula_clock_fast = (ula_control >> 4) & 1;
    c->ula_teletext = (ula_control >> 1) & 1;
}

CRTC_API uint8_t crtc_read(const struct crtc *c, int addr)
{
    if (addr == 0) {
        return 0;
    }
    int reg = c->addr_reg & 0x1f;
    if (reg >= CRTC_R12_MAH && reg <= CRTC_R17_LPL) {
        return c->reg[reg];
    }
    return 0;
}

CRTC_API void crtc_write(struct crtc *c, int addr, uint8_t val)
{
    if (addr == 0) {
        c->addr_reg = val;
        return;
    }
    int reg = c->addr_reg & 0x1f;
    if (reg >= CRTC_NUM_REGS) {
        return;
    }
    uint8_t mask = 0xff;
    switch (reg) {
    case CRTC_R4_VTOTAL: case CRTC_R6_VDISP: case CRTC_R7_VSYNC:
    case CRTC_R10_CSTART:
        mask = 0x7f;
        break;
    case CRTC_R5_VADJ: case CRTC_R9_MAXRA: case CRTC_R11_CEND:
        mask = 0x1f;
        break;
    case CRTC_R12_MAH: case CRTC_R14_CAH:
        mask = 0x3f;
        break;
    case CRTC_R16_LPH: case CRTC_R17_LPL:
        return;  // read only
    default:
        break;
    }
    c->reg[reg] = val & mask;

    switch (reg) {
    case CRTC_R0_HTOTAL:
        c->half_r0 = (c->reg[CRTC_R0_HTOTAL] + 1) / 2;
        break;
    case CRTC_R3_SYNCW:
        c->hsync_width = val & 0x0f;
        c->vsync_width = (val >> 4) ? (val >> 4) : 16;
        break;
    case CRTC_R8_INTERLACE:
        c->is_interlace = val & 1;
        c->is_isv = ((val & 3) == 3);
        c->skew_dispen_index = (val >> 4) & 3;
        c->cursor_skew = (val >> 6) & 3;
        if (c->is_isv) {
            c->scanline_stride = 2;
            c->scanline_mask = 0x1e;
        } else {
            c->scanline_stride = 1;
            c->scanline_mask = 0x1f;
        }
        crtc_update_cursor_disabled(c);
        crtc_update_vsync(c);
        break;
    case CRTC_R10_CSTART:
        c->cursor_flashing = (val >> 6) & 1;
        c->cursor_flash_mask = (val & 0x20) ? 0x10 : 0x08;
        c->cursor_start_line = val & 0x1f;
        crtc_update_cursor_disabled(c);
        break;
    default:
        break;
    }

    if (c->is_isv) {
        c->scanline_counter &= ~1;  // odd R9 in ISV mode is not modelled
    }
    if (reg == CRTC_R7_VSYNC) {
        // A new R7 can start a vsync at once, mid scanline.
        crtc_line_start_checks(c, 1);
    }
}

CRTC_API void crtc_reset(struct crtc *c)
{
    // beebjit's choice of power-on registers: a MODE 7 setup, so timing is
    // deterministic (real chips come up with random values).
    static const uint8_t init[CRTC_NUM_REGS] = {
        63, 40, 51, 0x24, 30, 2, 25, 28, 3 | (1 << 4) | (2 << 6), 18,
        0x72, 0x13, 0, 0, 0, 0, 0, 0,
    };
    uint8_t ula_clock_fast = c->ula_clock_fast;
    uint8_t ula_teletext = c->ula_teletext;
    uint16_t wrap = c->screen_wrap_add;
    *c = (struct crtc){0};
    c->ula_clock_fast = ula_clock_fast;
    c->ula_teletext = ula_teletext;
    c->screen_wrap_add = wrap;
    for (int i = 0; i < CRTC_NUM_REGS; i++) {
        c->reg[i] = init[i];
    }
    c->is_interlace = 1;
    c->is_isv = 1;
    c->scanline_stride = 2;
    c->scanline_mask = 0x1e;
    c->hsync_width = 4;
    c->vsync_width = 2;
    c->half_r0 = 32;
    c->cursor_flashing = 1;
    c->cursor_flash_mask = 0x10;
    c->cursor_start_line = 18;
    c->skew_dispen_index = 1;
    c->cursor_skew = 2;
    c->display_enable_bits = DISPEN_VERT;
    c->start_of_line_state_checks = 1;
    c->is_first_frame_scanline = 1;
    c->cursor_skew_counter = -1;
    crtc_line_start_checks(c, 1);
}
