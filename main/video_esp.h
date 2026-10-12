// esp32-beeb: the core 0 side of the picture.
//
// A task on core 0 drains the machine's video stream through the renderer
// (render.c, built here with 3-bit indexed pixels) and scales each raster
// straight into the panel's band buffers: 640 picture pixels to 448 panel
// columns by keeping 7 in 10, 256 scanlines to 336 panel rows by doubling 5
// in 16 (teletext's second raster supplies the doubled row). There is no
// frame buffer: a band is sent as soon as the beam leaves it, so the panel
// trails the emulated beam by about a band. Rows the beam never reaches in
// a frame stay black, as on a CRT.

#ifndef VIDEO_ESP_H
#define VIDEO_ESP_H

#include <stdint.h>

#include "vstream.h"

// Starts the render task on core 0, consuming vs.
void video_start(struct vstream *vs);

// Counters for the stats line.
extern volatile uint32_t video_frames;     // flybacks rendered
extern volatile uint32_t video_overruns;   // times the ring lapped the renderer
extern volatile uint32_t video_bands;      // band transfers queued
extern volatile uint32_t video_busy_us;    // core 0 time spent rendering
extern volatile uint32_t video_words;      // stream words consumed
// Panel path: cycles scaling rows, waiting for a band buffer, queueing a
// band; rows written.
extern volatile uint32_t video_clk_rows, video_clk_start, video_clk_flush, video_rows;

// Measurement only: 0 normal; 1 render but send nothing to the panel;
// 2 discard the stream without rendering.
extern volatile int video_test_mode;
// Renderer path counts since the last call (fast run words, slow words).
void video_path_stats(uint32_t *fast, uint32_t *slow);
void video_event_stats(uint32_t *tables, uint32_t *ulactrl, uint32_t *ulapal, uint32_t *events);
void video_clk_stats(uint32_t *tables, uint32_t *slow);   // cycles spent, since last call

#endif
