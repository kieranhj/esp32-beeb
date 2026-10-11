// esp32-beeb: CO5300 panel over QSPI, rotated to landscape. See display.h.
//
// The panel is driven straight through the SPI master driver rather than
// through esp_lcd's panel io: that io sends every command as a polling
// transaction and first drains the DMA queue, so a band could not be
// queued while the previous one was still in flight (measured: each band
// flush waited the length of a transfer, about 200 us). Here the window
// commands and the pixel data for a band are three queued transactions,
// and the renderer only ever blocks when both band buffers are in flight.
//
// The wire format is the one esp_lcd_co5300 uses: a single-line 8-bit
// opcode and 24-bit address (0x02 <reg> 00 for a register write, 0x32 00
// 2C 00 for pixel data), then the data on four lines for pixels. The init
// sequence is Waveshare's, as in esp32-fluidbox.

#include "display.h"

#include <string.h>

#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LCD_HOST SPI2_HOST
#define LCD_CS GPIO_NUM_12
#define LCD_PCLK GPIO_NUM_11
#define LCD_DATA0 GPIO_NUM_4
#define LCD_DATA1 GPIO_NUM_5
#define LCD_DATA2 GPIO_NUM_6
#define LCD_DATA3 GPIO_NUM_7

#define TOUCH_ADDR_CST820 0x15
// The V2 panel's memory starts 16 columns in on its native (portrait)
// column axis, which after the row/column exchange is our y.
#define V2_PANEL_GAP 0x10
#ifndef DISPLAY_GAP_X
#define DISPLAY_GAP_X 0
#endif
#ifndef DISPLAY_GAP_Y
#define DISPLAY_GAP_Y V2_PANEL_GAP
#endif

#define OPCODE_WRITE_CMD 0x02
#define OPCODE_WRITE_COLOR 0x32
#define CMD_SWRESET 0x01
#define CMD_CASET 0x2a
#define CMD_RASET 0x2b
#define CMD_RAMWR 0x2c
#define CMD_MADCTL 0x36
#define CMD_BRIGHTNESS 0x51

// 80 MHz is the ESP32-S3 SPI ceiling; FluidBox runs this panel there.
#define LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)

static const char *TAG = "display";

// Two buffers so one fills while the other is in flight. DMA_ATTR puts them
// in DMA-capable internal SRAM; PSRAM buffers do not work for this.
static DMA_ATTR uint16_t s_band_buf[2][DISPLAY_BAND_PIXELS];
// Window parameters per band, DMA readable, alive for the whole queue.
static DMA_ATTR uint8_t s_caset[4];
static DMA_ATTR uint8_t s_raset[DISPLAY_BANDS][4];

static spi_device_handle_t s_dev;
static SemaphoreHandle_t s_buffers_free;
static bool s_is_v2;

// Two transactions per band (row window, pixels), reused round robin.
static spi_transaction_t s_trans[2][2];
static unsigned s_trans_n;

// Measurement: cycle count at each flush and the time to its completion
// interrupt, so the transfer time of a band is known (see video_esp.c).
static uint32_t s_flush_clk[2];
static unsigned s_flush_n, s_done_n;
volatile uint32_t display_dma_clk, display_dma_count;

// Waveshare's power-on sequence: RGB565, brightness and HBM registers open,
// full-window address set, sleep out, display on.
struct init_cmd { uint8_t cmd; uint8_t data[4]; uint8_t len; uint8_t delay_ms; };
static const struct init_cmd s_init_cmds[] = {
    {0xFE, {0x00}, 1, 0},
    {0xC4, {0x80}, 1, 0},
    {0x3A, {0x55}, 1, 0},
    {0x35, {0x00}, 1, 0},
    {0x53, {0x20}, 1, 0},
    {0x51, {0xFF}, 1, 0},
    {0x63, {0xFF}, 1, 0},
    {0x2A, {0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, {0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x11, {0}, 0, 100},
    {0x29, {0}, 0, 0},
};

// Runs in interrupt context when a transaction has gone out.
static void IRAM_ATTR on_post_trans(spi_transaction_t *t)
{
    if (t->user == NULL) {
        return;   // a window command, not a band
    }
    display_dma_clk += esp_cpu_get_cycle_count() - s_flush_clk[s_done_n++ & 1];
    display_dma_count++;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_buffers_free, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

// A register write, sent synchronously (init and brightness only).
static esp_err_t write_reg(uint8_t reg, const uint8_t *data, size_t len)
{
    spi_transaction_t t = {
        .cmd = OPCODE_WRITE_CMD,
        .addr = (uint32_t)reg << 8,
        .length = len * 8,
        .tx_buffer = len ? data : NULL,
    };
    return spi_device_polling_transmit(s_dev, &t);
}

static bool detect_v2(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return true;
    }
    const bool is_v2 = i2c_master_probe(bus, TOUCH_ADDR_CST820, 50) == ESP_OK;
    ESP_LOGI(TAG, "%s board revision", is_v2 ? "V2 (CO5300/CST820)" : "original (SH8601/FT3168)");
    return is_v2;
}

esp_err_t display_init(i2c_master_bus_handle_t i2c_bus)
{
    s_is_v2 = detect_v2(i2c_bus);

    s_buffers_free = xSemaphoreCreateCounting(2, 2);
    if (s_buffers_free == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_PCLK,
        .data0_io_num = LCD_DATA0,
        .data1_io_num = LCD_DATA1,
        .data2_io_num = LCD_DATA2,
        .data3_io_num = LCD_DATA3,
        .max_transfer_sz = DISPLAY_BAND_PIXELS * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init failed");
    const spi_device_interface_config_t dev_config = {
        .command_bits = 8,
        .address_bits = 24,
        .mode = 0,
        .clock_speed_hz = LCD_PIXEL_CLOCK_HZ,
        .spics_io_num = LCD_CS,
        // Results are never fetched: completion is the post callback.
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_RETURN_RESULT,
        .queue_size = 8,
        .post_cb = on_post_trans,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(LCD_HOST, &dev_config, &s_dev), TAG, "spi device failed");
    int actual_hz = 0;
    spi_device_get_actual_freq(s_dev, &actual_hz);
    ESP_LOGI(TAG, "QSPI clock %d kHz", actual_hz);

    ESP_RETURN_ON_ERROR(write_reg(CMD_SWRESET, NULL, 0), TAG, "reset failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    for (size_t i = 0; i < sizeof s_init_cmds / sizeof s_init_cmds[0]; i++) {
        const struct init_cmd *c = &s_init_cmds[i];
        ESP_RETURN_ON_ERROR(write_reg(c->cmd, c->data, c->len), TAG, "init command failed");
        if (c->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
        }
    }
    // Landscape: row/column exchange (bit 5) and the chosen mirrors.
    uint8_t madctl = 0x20 | (DISPLAY_MIRROR_X ? 0x40 : 0) | (DISPLAY_MIRROR_Y ? 0x80 : 0);
    ESP_RETURN_ON_ERROR(write_reg(CMD_MADCTL, &madctl, 1), TAG, "madctl failed");

    // Window parameters for every band, computed once.
    const int gx = DISPLAY_GAP_X;
    const int gy = s_is_v2 ? DISPLAY_GAP_Y : 0;
    s_caset[0] = (uint8_t)(gx >> 8);
    s_caset[1] = (uint8_t)gx;
    s_caset[2] = (uint8_t)((gx + DISPLAY_W - 1) >> 8);
    s_caset[3] = (uint8_t)(gx + DISPLAY_W - 1);
    for (int b = 0; b < DISPLAY_BANDS; b++) {
        int y0 = gy + b * DISPLAY_BAND_ROWS, y1 = y0 + DISPLAY_BAND_ROWS - 1;
        s_raset[b][0] = (uint8_t)(y0 >> 8);
        s_raset[b][1] = (uint8_t)y0;
        s_raset[b][2] = (uint8_t)(y1 >> 8);
        s_raset[b][3] = (uint8_t)y1;
    }
    // The column window is the same for every band: set once here.
    ESP_RETURN_ON_ERROR(write_reg(CMD_CASET, s_caset, 4), TAG, "caset failed");
    return ESP_OK;
}

uint16_t *display_acquire_band(void)
{
    // Transfers complete in order, so once a slot frees the next buffer in
    // rotation is idle.
    xSemaphoreTake(s_buffers_free, portMAX_DELAY);
    static unsigned next;
    return s_band_buf[next++ & 1];
}

esp_err_t display_flush_band(int band, const uint16_t *buffer)
{
    s_flush_clk[s_flush_n++ & 1] = esp_cpu_get_cycle_count();
    spi_transaction_t *t = s_trans[s_trans_n++ & 1];
    memset(t, 0, sizeof s_trans[0]);
    t[0].cmd = OPCODE_WRITE_CMD;
    t[0].addr = CMD_RASET << 8;
    t[0].length = 32;
    t[0].tx_buffer = s_raset[band];
    t[1].cmd = OPCODE_WRITE_COLOR;
    t[1].addr = CMD_RAMWR << 8;
    t[1].flags = SPI_TRANS_MODE_QIO;
    t[1].length = DISPLAY_BAND_PIXELS * 16;
    t[1].tx_buffer = buffer;
    t[1].user = (void *)1;   // completion frees a band buffer
    for (int i = 0; i < 2; i++) {
        esp_err_t err = spi_device_queue_trans(s_dev, &t[i], portMAX_DELAY);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t display_set_brightness(uint8_t level)
{
    // Not while bands are queued: polling would collide with the queue.
    xSemaphoreTake(s_buffers_free, portMAX_DELAY);
    xSemaphoreTake(s_buffers_free, portMAX_DELAY);
    esp_err_t err = write_reg(CMD_BRIGHTNESS, &level, 1);
    xSemaphoreGive(s_buffers_free);
    xSemaphoreGive(s_buffers_free);
    return err;
}
