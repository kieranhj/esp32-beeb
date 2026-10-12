// esp32-beeb: the AMOLED panel, used sideways.
//
// The CO5300 is a 368 by 448 portrait panel; the Beeb picture is landscape,
// so the panel's MADCTL row/column exchange is switched on and everything
// here talks in landscape coordinates: DISPLAY_W wide (448, along the long
// axis) by DISPLAY_H high (368). The picture is drawn one band of
// DISPLAY_BAND_ROWS rows at a time from two DMA buffers in internal SRAM,
// the way FluidBox's display.c does for this board (see decisions.md for the
// board revision detection and the 16 pixel gap).

#ifndef BEEB_DISPLAY_H
#define BEEB_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define DISPLAY_W 448
#define DISPLAY_H 368
// 16 rows a band: 23 transfers a frame, 14 KB a buffer. Each transfer
// costs about 25 us of driver time, larger bands cost DRAM (see
// decisions.md); this is the compromise.
#define DISPLAY_BAND_ROWS 16
#define DISPLAY_BANDS (DISPLAY_H / DISPLAY_BAND_ROWS)   // 23
#define DISPLAY_BAND_PIXELS (DISPLAY_W * DISPLAY_BAND_ROWS)
#define DISPLAY_BAND_BUFFERS 3   // in flight or being filled

// Which way round the landscape picture sits on the portrait panel. The
// panel does the rotation; these only pick the direction. If the boot screen
// reads mirrored, flip DISPLAY_MIRROR_X; if upside down, flip both.
#ifndef DISPLAY_MIRROR_X
#define DISPLAY_MIRROR_X true
#endif
#ifndef DISPLAY_MIRROR_Y
#define DISPLAY_MIRROR_Y false
#endif

// Probes the touch controller to tell the board revision, brings the panel
// up rotated, full brightness. i2c_bus may be NULL (assumes V2).
esp_err_t display_init(i2c_master_bus_handle_t i2c_bus);

// Blocks until one of the two band buffers is free, then returns it. The
// buffer holds DISPLAY_BAND_PIXELS RGB565 pixels, byte swapped for the panel
// (see display_rgb565), row major, DISPLAY_W per row.
uint16_t *display_acquire_band(void);

// Queues an asynchronous transfer of one band to rows
// band * DISPLAY_BAND_ROWS onwards. Returns at once.
esp_err_t display_flush_band(int band, const uint16_t *buffer);

// 0..255.
esp_err_t display_set_brightness(uint8_t level);

// Measurement: cycles from flush to completion, summed, and how many.
extern volatile uint32_t display_dma_clk, display_dma_count;

// A pixel value for the band buffers from 8-bit components.
static inline uint16_t display_rgb565(int r, int g, int b)
{
    uint16_t v = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    return (uint16_t)((v >> 8) | (v << 8));
}

#endif
