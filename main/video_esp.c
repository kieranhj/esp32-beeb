// esp32-beeb: core 0 renderer and scaler. See video_esp.h.

#include "video_esp.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

// The renderer is built into this file with the board's pixel type and its
// output going to the scaler below instead of a canvas.
#define RENDER_INDEXED 1
#define RENDER_FN IRAM_ATTR
#define RENDER_EMIT(r, first, count, vert) video_emit(r, first, count, vert)
#define RENDER_FRAME_HOOK(r) video_frame_done()
#define RENDER_LINE2_WANTED(r) video_line2_wanted(r)
struct render;
static int video_line2_wanted(const struct render *r);
struct render;
static void video_emit(struct render *r, uint32_t first, uint32_t count, int32_t vert);
static void video_frame_done(void);
#include "render.c"
#include "teletext.c"

static const char *TAG = "video";

volatile uint32_t video_frames;
volatile uint32_t video_overruns;
volatile uint32_t video_bands;
volatile uint32_t video_busy_us;
volatile uint32_t video_words;
volatile int video_test_mode;
// Cycle counters for the benchmark: row scaling, band start (acquire and
// clear), band flush (the driver's command transactions).
volatile uint32_t video_clk_rows, video_clk_start, video_clk_flush, video_rows;
#define s_clk_rows video_clk_rows
#define s_clk_start video_clk_start
#define s_clk_flush video_clk_flush
#define s_rows video_rows

// The picture inside the canvas: beebjit's standard window puts the 640 by
// 512 (two rasters per scanline) picture 64 pixels in from the canvas edge
// on each side.
#define PIC_X0 (RENDER_BORDER_CHARS * 16)
#define PIC_Y0 (RENDER_BORDER_CHARS * 16)
#define PIC_W 640
#define PIC_RASTERS 512
#define PIC_ROWS 336                      // 256 scanlines * 21 / 16
#define PIC_TOP ((DISPLAY_H - PIC_ROWS) / 2)   // 16 rows of border above

static struct render s_render;
static struct vstream *s_vs;

// Canvas x for each panel column (keep 7 of every 10), and for each canvas
// x the first panel column that takes a pixel at or beyond it.
static uint16_t s_src_x[DISPLAY_W];
static uint16_t s_dst_from_src[RENDER_WIDTH + 1];
// The 8 physical colours as panel pixels, indexed by the ULA's BGR bits.
static uint16_t s_rgb[8];

// The band being filled, where it is on the panel, and which of its rows
// have been written (the rest are cleared when it is sent).
static uint16_t *s_buf;
static int s_band;
static uint32_t s_rows_written;
// The last row scaled, so a doubled row in a bitmap mode is a copy.
static uint16_t *s_last_row;

static void build_tables(void)
{
    for (int i = 0; i < DISPLAY_W; i++) {
        s_src_x[i] = (uint16_t)(PIC_X0 + (i * 10) / 7);
    }
    int d = 0;
    for (int x = 0; x <= RENDER_WIDTH; x++) {
        while (d < DISPLAY_W && s_src_x[d] < x) {
            d++;
        }
        s_dst_from_src[x] = (uint16_t)d;
    }
    for (int i = 0; i < 8; i++) {
        s_rgb[i] = display_rgb565((i & 1) ? 255 : 0, (i & 2) ? 255 : 0, (i & 4) ? 255 : 0);
    }
}

static RENDER_FN void start_band(int band)
{
    uint32_t c0 = esp_cpu_get_cycle_count();
    s_band = band;
    s_buf = display_acquire_band();
    s_rows_written = 0;
    s_clk_start += esp_cpu_get_cycle_count() - c0;
}

static RENDER_FN void flush_band(void)
{
    uint32_t c0 = esp_cpu_get_cycle_count();
    // Rows the beam never reached are black, as on a CRT.
    for (int row = 0; row < DISPLAY_BAND_ROWS; row++) {
        if (!(s_rows_written & (1u << row))) {
            memset(s_buf + row * DISPLAY_W, 0, DISPLAY_W * sizeof(uint16_t));
        }
    }
    display_flush_band(s_band, s_buf);
    video_bands++;
    s_clk_flush += esp_cpu_get_cycle_count() - c0;
}

// Moves to the band holding panel row y, sending the ones passed over.
static inline RENDER_FN void seek_band(int band)
{
    if (band < s_band) {
        // Rows went backwards without a flyback: the renderer resynced
        // after an overrun. Jump rather than wrap through every band.
        flush_band();
        start_band(band);
        return;
    }
    while (s_band != band) {
        flush_band();
        start_band((s_band + 1) % DISPLAY_BANDS);
    }
}

// Scanline s of the picture covers panel rows [s*21/16, (s+1)*21/16): two
// rows for 5 scanlines in 16, and only those read the second raster.
static RENDER_FN int video_line2_wanted(const struct render *r)
{
    int32_t cy = (r->vert_beam_pos & ~1) - r->vert_window_start;
    int32_t raster = cy - PIC_Y0;
    if (raster < 0 || raster >= PIC_RASTERS) {
        return 0;
    }
    int s = raster >> 1;
    return (((s + 1) * 21) >> 4) - ((s * 21) >> 4) == 2;
}

static RENDER_FN void write_row(const render_px *px, uint32_t first, uint32_t count, int y)
{
    if (video_test_mode == 1) {
        return;
    }
    seek_band(y / DISPLAY_BAND_ROWS);
    uint32_t c0 = esp_cpu_get_cycle_count();
    s_rows++;
    int row = y - s_band * DISPLAY_BAND_ROWS;
    uint16_t *dst = s_buf + row * DISPLAY_W;
    s_last_row = dst;
    uint32_t i0 = s_dst_from_src[first];
    uint32_t i1 = s_dst_from_src[first + count];
    if (!(s_rows_written & (1u << row))) {
        // First touch this frame: whatever the span leaves is black.
        s_rows_written |= 1u << row;
        if (i0 > 0) {
            memset(dst, 0, i0 * sizeof(uint16_t));
        }
        if (i1 < DISPLAY_W) {
            memset(dst + i1, 0, (DISPLAY_W - i1) * sizeof(uint16_t));
        }
    }
    // 7 panel pixels per 10 picture pixels: source offsets 0 1 2 4 5 7 8
    // within each 10, two groups per iteration so stores are whole words.
    uint32_t g0 = (i0 + 13) / 14, g1 = i1 / 14;
    for (uint32_t i = i0; i < g0 * 14 && i < i1; i++) {
        dst[i] = s_rgb[px[s_src_x[i]]];
    }
    const uint16_t *rgb = s_rgb;
    for (uint32_t g = g0; g < g1; g++) {
        const render_px *sp = px + PIC_X0 + 20 * g;
        uint32_t *d = (uint32_t *)(dst + 14 * g);
        d[0] = rgb[sp[0]] | ((uint32_t)rgb[sp[1]] << 16);
        d[1] = rgb[sp[2]] | ((uint32_t)rgb[sp[4]] << 16);
        d[2] = rgb[sp[5]] | ((uint32_t)rgb[sp[7]] << 16);
        d[3] = rgb[sp[8]] | ((uint32_t)rgb[sp[10]] << 16);
        d[4] = rgb[sp[11]] | ((uint32_t)rgb[sp[12]] << 16);
        d[5] = rgb[sp[14]] | ((uint32_t)rgb[sp[15]] << 16);
        d[6] = rgb[sp[17]] | ((uint32_t)rgb[sp[18]] << 16);
    }
    for (uint32_t i = g1 * 14 > i0 ? g1 * 14 : i0; i < i1; i++) {
        dst[i] = s_rgb[px[s_src_x[i]]];
    }
    s_clk_rows += esp_cpu_get_cycle_count() - c0;
}

static RENDER_FN void video_emit(struct render *r, uint32_t first, uint32_t count, int32_t vert)
{
    int32_t cy = (vert & ~1) - r->vert_window_start;
    int32_t raster = cy - PIC_Y0;
    if (raster < 0 || raster >= PIC_RASTERS || count == 0) {
        return;
    }
    // Scanline s covers panel rows [s*21/16, (s+1)*21/16): one or two.
    int s = raster >> 1;
    int row0 = PIC_TOP + ((s * 21) >> 4);
    int row1 = PIC_TOP + (((s + 1) * 21) >> 4);
    write_row(r->line, first, count, row0);
    if (row1 - row0 == 2) {
        if (r->mode != RENDER_MODE7 && video_test_mode != 1 && (row0 + 1) / DISPLAY_BAND_ROWS == s_band) {
            // Same raster twice: copy the scaled row (teletext's second
            // raster differs, so it is scaled).
            const uint16_t *src = s_last_row;
            uint32_t c0 = esp_cpu_get_cycle_count();
            s_rows++;
            int row = row0 + 1 - s_band * DISPLAY_BAND_ROWS;
            s_rows_written |= 1u << row;
            memcpy(s_buf + row * DISPLAY_W, src, DISPLAY_W * sizeof(uint16_t));
            s_clk_rows += esp_cpu_get_cycle_count() - c0;
        } else {
            write_row(r->line2, first, count, row0 + 1);
        }
    }
}

static RENDER_FN void video_frame_done(void)
{
    video_frames++;
    if (video_test_mode == 1) {
        return;
    }
    // Send the rest of the panel, black where the beam did not reach, and
    // start again from the top.
    seek_band(DISPLAY_BANDS - 1);
    flush_band();
    start_band(0);
}

static void render_task(void *arg)
{
    (void)arg;
    render_init(&s_render, NULL);
    start_band(0);
    ESP_LOGI(TAG, "render task on core %d", xPortGetCoreID());
    for (;;) {
        int n = vs_available(s_vs);
        if (n <= 0) {
            vTaskDelay(1);
            continue;
        }
        if (n > VS_RING) {
            // The machine lapped us; the oldest words are gone. Skip ahead to
            // the newest half ring and carry on; the picture tears once.
            video_overruns++;
            s_vs->tail = s_vs->head - VS_RING / 2;
            n = VS_RING / 2;
        }
        if (n > 4096) {
            n = 4096;
        }
        if (video_test_mode == 2) {
            s_vs->tail += (uint32_t)n;
            video_words += (uint32_t)n;
            vTaskDelay(1);
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        video_words += (uint32_t)n;
        render_consume_n(&s_render, s_vs, n);
        video_busy_us += (uint32_t)(esp_timer_get_time() - t0);
    }
}

// Measurement: times the renderer over a synthetic MODE 2 stream already
// in the ring, so its own speed is known apart from the machine's.
static void video_bench(struct vstream *vs, const char *when, int mode7)
{
    struct render *r = &s_render;
    render_init(r, NULL);
    start_band(0);
    vs->head = vs->tail = 0;
    vs_put(vs, VS_EV(VS_ULACTRL, mode7 ? 0x4b : 0xf4));   // MODE 7 or MODE 2
    uint32_t n = 0;
    while (vs->head + 8 * 130 <= VS_RING) {
        for (int line = 0; line < 8; line++) {
            vs_put(vs, VS_EV(VS_RA, line));
            for (int t = 0; t < 128; t++) {
                uint8_t data = (uint8_t)(t * 7 + line * 3 + n);
                if (mode7) {
                    // one pair word per character, as the CRTC emits
                    data = (uint8_t)(0x20 + ((t / 2 + n) % 0x5f));   // printable text
                    vs_put(vs, (uint16_t)((data << 8) | VS_PAIR | (t < 80 ? VS_DISPEN : 0) | VS_MA13));
                    t++;
                    continue;
                }
                vs_put(vs, (uint16_t)((data << 8) | (t & 1) | (t < 80 ? VS_DISPEN : 0)));
            }
            vs_put(vs, VS_EV(VS_HSYNC, 8));
        }
        n++;
    }
    uint32_t words = vs->head;
    static const char *const names[] = {"full", "all slow path"};
    for (int variant = 0; variant < 2; variant++) {
        render_bench_skip = variant;
        vs->tail = 0;
        r->stat_fast_words = r->stat_slow_words = 0;
        int64_t u0 = esp_timer_get_time();
        uint32_t c0 = esp_cpu_get_cycle_count();
        render_consume_n(r, vs, (int)words);
        uint32_t c = esp_cpu_get_cycle_count() - c0;
        int64_t us = esp_timer_get_time() - u0;
        printf("BENCH render %s %s, %s: %lu words (%lu fast, %lu slow), %lu clk/word, %lld us\n",
               mode7 ? "MODE 7" : "MODE 2", when, names[variant], (unsigned long)words, (unsigned long)r->stat_fast_words,
               (unsigned long)r->stat_slow_words, (unsigned long)(c / words), (long long)us);
    }
    render_bench_skip = 0;
    // the same again with the panel writes
    video_test_mode = 0;
    vs->tail = 0;
    s_clk_rows = s_clk_start = s_clk_flush = s_rows = 0;
    uint32_t bands0 = video_bands;
    display_dma_clk = display_dma_count = 0;
    uint32_t c0 = esp_cpu_get_cycle_count();
    render_consume_n(r, vs, (int)words);
    uint32_t c = esp_cpu_get_cycle_count() - c0;
    printf("BENCH render+panel %s %s: %lu clk/word (rows %lu, band start %lu, band flush %lu per word; %lu rows at %lu clk, %lu bands, "
           "flush call %lu us, flush to done %lu us)\n",
           mode7 ? "MODE 7" : "MODE 2", when, (unsigned long)(c / words), (unsigned long)(s_clk_rows / words),
           (unsigned long)(s_clk_start / words), (unsigned long)(s_clk_flush / words),
           (unsigned long)(s_rows), (unsigned long)(s_rows ? s_clk_rows / s_rows : 0),
           (unsigned long)(video_bands - bands0),
           (unsigned long)(s_clk_flush / (video_bands - bands0) / 240),
           (unsigned long)(display_dma_count ? display_dma_clk / display_dma_count / 240 : 0));
    vs->head = vs->tail = 0;
    // Hand the band buffer back: the render task acquires its own.
    flush_band();
}

void video_start(struct vstream *vs)
{
    s_vs = vs;
    build_tables();
    video_test_mode = 1;
    video_bench(vs, "core 1 idle", 0);
    video_test_mode = 1;
    video_bench(vs, "core 1 idle", 1);
    video_test_mode = 0;
    xTaskCreatePinnedToCore(render_task, "render", 8192, NULL, 5, NULL, 0);
}
