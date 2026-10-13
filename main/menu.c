// esp32-beeb: the disc menu (see menu.h).
#include "menu.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "catalog.h"
#include "console.h"
#include "display.h"
#include "input.h"

static const char *TAG = "menu";

// The 8x8 character set, characters 32 to 127, from the Master's terminal
// ROM (where MOS 3.20 keeps it), already in the image for the machine.
extern const uint8_t terminal_rom_start[] asm("_binary_terminal_rom_start");
#define FONT_OFFSET 0x3900

// Text cells: the font doubled each way, so a row is one 16-row band.
#define CELL_W 16
#define COLS (DISPLAY_W / CELL_W)          // 28
#define ROWS DISPLAY_BANDS                 // 23
#define FIRST_ITEM_ROW 2
#define ITEM_ROWS (ROWS - FIRST_ITEM_ROW - 2)   // 19 a page, two rows of help below

struct text_row {
    char text[COLS + 1];
    uint16_t fg, bg;
};
EXT_RAM_BSS_ATTR static struct text_row s_rows[ROWS];

static void set_row(int r, uint16_t fg, uint16_t bg, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void set_row(int r, uint16_t fg, uint16_t bg, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_rows[r].text, sizeof s_rows[r].text, fmt, ap);
    va_end(ap);
    s_rows[r].fg = fg;
    s_rows[r].bg = bg;
}

// One text row into one band, in the panel's rotated layout (display.h).
static void draw_row(int r)
{
    uint16_t *band = display_acquire_band();
    const struct text_row *t = &s_rows[r];
    size_t len = strlen(t->text);
    for (int x = 0; x < DISPLAY_W; x++) {
        int col = x / CELL_W;
        unsigned ch = (size_t)col < len ? (unsigned char)t->text[col] : ' ';
        if (ch < 32 || ch > 127) {
            ch = '?';
        }
        const uint8_t *glyph = terminal_rom_start + FONT_OFFSET + (ch - 32) * 8;
        int bit = 7 - (x % CELL_W) / 2;
        uint16_t *dst = band + x * DISPLAY_BAND_ROWS;
        for (int y = 0; y < DISPLAY_BAND_ROWS; y++) {
            int on = (glyph[y / 2] >> bit) & 1;
            dst[DISPLAY_BAND_ROWS - 1 - y] = on ? t->fg : t->bg;
        }
    }
    display_flush_band(r, band);
}

static int s_page, s_highlight = -1;

static void build(void)
{
    const uint16_t black = display_rgb565(0, 0, 0), white = display_rgb565(255, 255, 255);
    const uint16_t yellow = display_rgb565(255, 255, 0), cyan = display_rgb565(0, 255, 255);
    const uint16_t green = display_rgb565(0, 255, 0), blue = display_rgb565(0, 0, 255);
    int n = catalog_count();
    int pages = (n + ITEM_ROWS - 1) / ITEM_ROWS;
    if (pages < 1) {
        pages = 1;
    }
    char cur[32];
    console_get_disc(cur, sizeof cur);
    set_row(0, yellow, blue, " esp32-beeb  %d discs  %d/%d", n, s_page + 1, pages);
    set_row(1, white, black, "%s", "");
    for (int i = 0; i < ITEM_ROWS; i++) {
        int k = s_page * ITEM_ROWS + i;
        const struct catalog_entry *e = catalog_entry(k);
        if (e == NULL) {
            set_row(FIRST_ITEM_ROW + i, white, black, "%s", "");
            continue;
        }
        char name[sizeof e->name];
        memcpy(name, e->name, sizeof name);   // the extension goes before the row cuts it
        char *dot = strrchr(name, '.');
        if (dot && e->sides == 1) {
            *dot = 0;   // .ssd is the usual case; .dsd stays visible
        }
        uint16_t fg = e->on_sd ? green : white;
        if (k == s_highlight) {
            set_row(FIRST_ITEM_ROW + i, black, cyan, " %-*.*s", COLS - 1, COLS - 1, name);
        } else {
            set_row(FIRST_ITEM_ROW + i, fg, black, "%c%.*s", strcmp(e->name, cur) == 0 ? '>' : ' ', COLS - 1, name);
        }
    }
    set_row(ROWS - 2, cyan, black, "%s", " tap: choose, again: boot");
    set_row(ROWS - 1, cyan, black, "%s", " swipe: page   PWR: back");
}

static void draw_all(void)
{
    build();
    for (int r = 0; r < ROWS; r++) {
        draw_row(r);
    }
}

static void boot(int k)
{
    const struct catalog_entry *e = catalog_entry(k);
    ESP_LOGI(TAG, "boot %s", e->name);
    console_set_disc(e->name);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

void menu_run(void)
{
    char cur[32];
    console_get_disc(cur, sizeof cur);
    int k = catalog_find(cur);
    if (k >= 0) {
        s_page = k / ITEM_ROWS;
    }
    draw_all();
    input_set_menu(1);
    for (;;) {
        int kind, x, y;
        if (!input_take_gesture(&kind, &x, &y)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        int n = catalog_count();
        int pages = (n + ITEM_ROWS - 1) / ITEM_ROWS;
        if (kind == INPUT_SWIPE_FORWARD || kind == INPUT_SWIPE_BACK) {
            if (pages > 1) {
                s_page = (s_page + (kind == INPUT_SWIPE_FORWARD ? 1 : pages - 1)) % pages;
                draw_all();
            }
        } else if (kind == INPUT_SWIPE_ACROSS) {
            ESP_LOGI(TAG, "leave");
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();
        } else if (kind == INPUT_TAP) {
            int row = y / DISPLAY_BAND_ROWS;
            int i = row - FIRST_ITEM_ROW;
            int k = s_page * ITEM_ROWS + i;
            ESP_LOGI(TAG, "tap at picture %d,%d: row %d", x, y, row);
            if (i < 0 || i >= ITEM_ROWS || k >= n) {
                continue;
            }
            if (k == s_highlight) {
                boot(k);
            }
            int old = s_highlight;
            s_highlight = k;
            build();
            if (old >= s_page * ITEM_ROWS && old < (s_page + 1) * ITEM_ROWS) {
                draw_row(FIRST_ITEM_ROW + old - s_page * ITEM_ROWS);
            }
            draw_row(row);
        }
    }
}
