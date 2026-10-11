// esp32-beeb: the Master on core 1, the picture on core 0.
//
// Core 1 runs the machine paced to real time (2 M cycles per second of
// wall clock, in slices of at most a frame), its video stream crossing to
// the render task that video_esp.c pins to core 0. Every few seconds a
// line of statistics goes to the serial port: host clocks per emulated
// cycle with the renderer busy, how far behind real time the machine got,
// frames and bands presented, renderer load.
//
// With no keyboard yet, a short BASIC program is typed in through the
// keyboard matrix after boot so there is something moving to look at.

#define MACHINE_STATS 1
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "machine.h"
#include "video_esp.h"

static const char *TAG = "beeb";

#define I2C_PORT I2C_NUM_0
#define I2C_SDA GPIO_NUM_15
#define I2C_SCL GPIO_NUM_14

#define DISPLAY_BRIGHTNESS 230
#define STATS_PERIOD_US 5000000

// The ROM set, read in place from flash through the data cache.
#define ROM(sym) \
    extern const uint8_t sym##_start[] asm("_binary_" #sym "_rom_start");
ROM(mos) ROM(dfs) ROM(viewsht) ROM(edit) ROM(basic4) ROM(adfs) ROM(view) ROM(terminal)

// Everything the emulation touches per cycle lives in internal SRAM.
static DRAM_ATTR struct machine machine;
static DRAM_ATTR struct vstream stream;

// Keyboard matrix positions (row, column) for the characters the typed
// script needs; the same table as tools/frametest.py.
struct keypos { char ch; uint8_t row, col; };
static const struct keypos s_keys[] = {
    {'A', 4, 1}, {'B', 6, 4}, {'C', 5, 2}, {'D', 3, 2}, {'E', 2, 2}, {'F', 4, 3},
    {'G', 5, 3}, {'H', 5, 4}, {'I', 2, 5}, {'J', 4, 5}, {'K', 4, 6}, {'L', 5, 6},
    {'M', 6, 5}, {'N', 5, 5}, {'O', 3, 6}, {'P', 3, 7}, {'Q', 1, 0}, {'R', 3, 3},
    {'S', 5, 1}, {'T', 2, 3}, {'U', 3, 5}, {'V', 6, 3}, {'W', 2, 1}, {'X', 4, 2},
    {'Y', 4, 4}, {'Z', 6, 1},
    {'0', 2, 7}, {'1', 3, 0}, {'2', 3, 1}, {'3', 1, 1}, {'4', 1, 2}, {'5', 1, 3},
    {'6', 3, 4}, {'7', 2, 4}, {'8', 1, 5}, {'9', 2, 6},
    {' ', 6, 2}, {',', 6, 6}, {'.', 6, 7}, {'/', 6, 8}, {';', 5, 7}, {':', 4, 8},
    {'-', 1, 7}, {'\n', 4, 9},
};
// Characters reached with shift held, and the key they sit on.
static const char s_shifted[][2] = {
    {'%', '5'}, {'*', ':'}, {'+', ';'}, {'=', '-'}, {'(', '8'}, {')', '9'},
    {'<', ','}, {'>', '.'}, {'!', '1'}, {'"', '2'}, {'&', '6'}, {'\'', '7'},
    {'?', '/'},
};
#define SHIFT_ROW 0
#define SHIFT_COL 0

static const struct keypos *find_key(char c)
{
    for (size_t i = 0; i < sizeof s_keys / sizeof s_keys[0]; i++) {
        if (s_keys[i].ch == c) {
            return &s_keys[i];
        }
    }
    return NULL;
}

// Queues text to be typed from the given cycle: each key held for hold
// cycles, one key every step cycles.
static uint32_t type_text(struct machine *m, const char *text, uint32_t cycle,
                          uint32_t step, uint32_t hold)
{
    for (; *text; text++) {
        char c = *text;
        int shift = 0;
        for (size_t i = 0; i < sizeof s_shifted / sizeof s_shifted[0]; i++) {
            if (s_shifted[i][0] == c) {
                c = s_shifted[i][1];
                shift = 1;
            }
        }
        const struct keypos *k = find_key(c);
        if (!k) {
            ESP_LOGW(TAG, "no key for '%c'", *text);
            continue;
        }
        if (shift) {
            machine_queue_key(m, cycle - 20000, SHIFT_ROW, SHIFT_COL, 1);
        }
        machine_queue_key(m, cycle, k->row, k->col, 1);
        machine_queue_key(m, cycle + hold, k->row, k->col, 0);
        if (shift) {
            machine_queue_key(m, cycle + hold + 20000, SHIFT_ROW, SHIFT_COL, 0);
        }
        cycle += step;
    }
    return cycle;
}

static void beeb_task(void *arg)
{
    (void)arg;
    struct machine *m = &machine;

    machine_init(m);
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) {
        // PSRAM for now; see decisions.md.
        m->swram[i] = heap_caps_malloc(0x4000, MALLOC_CAP_SPIRAM);
    }
    m->mos = mos_start;
    m->rom[0x9] = dfs_start;
    m->rom[0xa] = viewsht_start;
    m->rom[0xb] = edit_start;
    m->rom[0xc] = basic4_start;
    m->rom[0xd] = adfs_start;
    m->rom[0xe] = view_start;
    m->rom[0xf] = terminal_start;
    machine_reset(m);
#ifdef BEEB_MEASURE
    // Measurement: the first 5 s run with no stream at all (mode 3), as the
    // Phase 2 benchmark did; then the stream dropped, rendered unsent, and
    // normal, 5 s each, so the costs can be told apart.
    video_test_mode = 3;
    uint32_t t = 50000000;   // typing starts after the measurement
#else
    m->vs = &stream;
    uint32_t t = 10000000;   // 5 s after power on
#endif

    // A mode 2 line drawer that runs forever, typed at the prompt.
    t = type_text(m, "MODE 2\n", t, 160000, 80000);
    t = type_text(m, "REPEAT:GCOL 0,RND(8):MOVE RND(1280),RND(1024):DRAW RND(1280),RND(1024):UNTIL 0\n",
                  t, 160000, 80000);

    printf("BEEB begin on core %d, machine %u bytes, stream %u bytes, internal heap free %u\n",
           xPortGetCoreID(), (unsigned)sizeof machine, (unsigned)sizeof stream,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    int64_t t0 = esp_timer_get_time();
    int64_t next_report = t0 + STATS_PERIOD_US;
    uint64_t clk = 0, cyc = 0;
    uint64_t max_lag = 0;
    uint32_t slices = 0, sleeps = 0;
    int reports = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        uint64_t due = (uint64_t)(now - t0) * 2;   // 2 MHz
        if (m->cpu.cycles >= due) {
            sleeps++;
            vTaskDelay(1);
        } else {
            // Run 2 ms ahead of real time so the next sleep is worth having;
            // never more than a frame in one call.
            uint64_t target = due + 4000;
            if (target > m->cpu.cycles + 40000) {
                target = m->cpu.cycles + 40000;
            }
            uint64_t before = m->cpu.cycles;
            uint32_t c0 = esp_cpu_get_cycle_count();
            machine_run(m, target);
            clk += (uint32_t)(esp_cpu_get_cycle_count() - c0);
            cyc += m->cpu.cycles - before;
            slices++;
            uint64_t lag = due - m->cpu.cycles;
            if ((int64_t)lag > 0 && lag > max_lag) {
                max_lag = lag;
            }
        }
        if (now >= next_report) {
            next_report += STATS_PERIOD_US;
            uint32_t words = video_words;
            printf("PANEL rows %lu (%lu us) band wait %lu us flush %lu us\n",
                   (unsigned long)video_rows, (unsigned long)(video_clk_rows / 240),
                   (unsigned long)(video_clk_start / 240), (unsigned long)(video_clk_flush / 240));
            video_rows = video_clk_rows = video_clk_start = video_clk_flush = 0;
            printf("STATS mode %d  %6.1f clk/cyc  lag max %llu cyc  slices %lu sleeps %lu  "
                   "frames %lu bands %lu overruns %lu  render %lu%% %lu ns/word  pc=%04X\n",
                   video_test_mode,
                   cyc ? (double)clk / (double)cyc : 0.0, (unsigned long long)max_lag,
                   (unsigned long)slices, (unsigned long)sleeps,
                   (unsigned long)video_frames, (unsigned long)video_bands,
                   (unsigned long)video_overruns,
                   (unsigned long)(video_busy_us / (STATS_PERIOD_US / 100)),
                   (unsigned long)(words ? (uint64_t)video_busy_us * 1000 / words : 0),
                   m->cpu.pc);
            clk = cyc = 0;
            max_lag = 0;
            slices = sleeps = 0;
            video_busy_us = 0;
            video_words = 0;
#ifdef BEEB_MEASURE
            // 5 s of each test mode, then normal running.
            reports++;
            video_test_mode = reports < 2 ? 2 : reports < 3 ? 1 : 0;
            m->vs = &stream;
#else
            (void)reports;
#endif
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "esp32-beeb");
    ESP_LOGI(TAG, "internal heap free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    i2c_master_bus_handle_t i2c = NULL;
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&cfg, &i2c) != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus failed; assuming V2 board");
        i2c = NULL;
    }
    ESP_ERROR_CHECK(display_init(i2c));
    ESP_ERROR_CHECK(display_set_brightness(DISPLAY_BRIGHTNESS));

    memset(&stream, 0, sizeof stream);
    video_start(&stream);
    xTaskCreatePinnedToCore(beeb_task, "beeb", 16384, NULL, 5, NULL, 1);
}
