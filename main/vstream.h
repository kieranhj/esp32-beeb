// esp32-beeb: the video stream between the machine (core 1) and the
// renderer (core 0, or the host frame tool).
//
// The CRTC and video ULA side of the machine produces one 16-bit word per
// 2 MHz tick plus tagged events, in the order beebjit's video code calls its
// renderer, so a renderer that follows beebjit's beam model produces the same
// pixels. Words:
//
//   tick:  bit 15 clear. bits 8-15 data byte, bits 0-7 flags:
//          VS_ODD     the second half of a 1 MHz character
//          VS_DISPEN  display enable after the R8 skew
//          VS_MA13    MA13 was set for this fetch (teletext addressing)
//   event: bit 15 set. bits 8-14 type, bits 0-7 payload:
//          VS_HSYNC   payload = hsync pulse width in ticks
//          VS_VSYNC   payload = new level (1 raise, 0 lower)
//          VS_RA      payload = row address | (ISV << 7)
//          VS_CURSOR  cursor starts at the next tick
//          VS_ULACTRL payload = video ULA control register
//          VS_ULAPAL  payload = video ULA palette write byte
//          VS_FRAME   frame boundary marker from the CRTC (debug aid)
//
// The producer writes into a ring of VS_RING words; the consumer reads at
// its own pace. On the board the ring is in internal SRAM and the consumer
// runs on the other core; on the host both run in one thread.

#ifndef VSTREAM_H
#define VSTREAM_H

#include <stdint.h>

enum {
    VS_ODD = 0x01,
    VS_DISPEN = 0x02,
    VS_MA13 = 0x04,
};

enum {
    VS_EVENT = 0x8000,
    VS_HSYNC = 0x8100,
    VS_VSYNC = 0x8200,
    VS_RA = 0x8300,
    VS_CURSOR = 0x8400,
    VS_ULACTRL = 0x8500,
    VS_ULAPAL = 0x8600,
    VS_FRAME = 0x8700,
};

#ifndef VS_RING
#define VS_RING 32768   // words; a power of two
#endif

struct vstream {
    volatile uint32_t head;   // producer index, free running
    volatile uint32_t tail;   // consumer index, free running
    uint16_t ring[VS_RING];
};

static inline void vs_put(struct vstream *vs, uint16_t w)
{
    uint32_t h = vs->head;
    vs->ring[h & (VS_RING - 1)] = w;
    vs->head = h + 1;
}

static inline int vs_available(const struct vstream *vs)
{
    return (int)(vs->head - vs->tail);
}

static inline uint16_t vs_get(struct vstream *vs)
{
    uint32_t t = vs->tail;
    uint16_t w = vs->ring[t & (VS_RING - 1)];
    vs->tail = t + 1;
    return w;
}

#endif
