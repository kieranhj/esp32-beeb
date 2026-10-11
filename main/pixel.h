// esp32-beeb: the renderer's pixel type.
//
// On the host the renderer produces beebjit's 0xAARRGGBB words so frames can
// be diffed against it. On the board (RENDER_INDEXED) a pixel is the 3-bit
// physical colour, BGR bit order as the ULA's palette register: the panel
// path expands it through a 16-bit table, and the tables and line buffers
// shrink by four. Teletext's anti-aliased glyph edges are the one thing
// lost: a blend becomes whichever of the two colours is nearer.

#ifndef PIXEL_H
#define PIXEL_H

#include <stdint.h>
#include <string.h>

// Placement of the renderer's per-tick code: the board puts it in IRAM
// (flash-cached code costs it more than the rendering itself, measured).
#ifndef RENDER_FN
#define RENDER_FN
#endif

#ifdef RENDER_INDEXED
typedef uint8_t render_px;
#define PX_BLACK 0
// r, g, b are 0 or 1.
#define PX_RGB(r, g, b) ((render_px)((r) | ((g) << 1) | ((b) << 2)))
#define PX_INVERT(p) ((render_px)((p) ^ 7))
// v is 0..255 coverage of fg over bg.
#define PX_BLEND(v, fg, bg) ((v) >= 128 ? (fg) : (bg))
// Eight pixels at a time; both sides are 4-byte aligned by construction.
#define PX_COPY8(dst, src) do { \
        ((uint32_t *)(dst))[0] = ((const uint32_t *)(src))[0]; \
        ((uint32_t *)(dst))[1] = ((const uint32_t *)(src))[1]; \
    } while (0)
#define PX_COPY16(dst, src) do { \
        ((uint32_t *)(dst))[0] = ((const uint32_t *)(src))[0]; \
        ((uint32_t *)(dst))[1] = ((const uint32_t *)(src))[1]; \
        ((uint32_t *)(dst))[2] = ((const uint32_t *)(src))[2]; \
        ((uint32_t *)(dst))[3] = ((const uint32_t *)(src))[3]; \
    } while (0)
#else
typedef uint32_t render_px;
#define PX_BLACK 0xff000000u
#define PX_RGB(r, g, b) (0xff000000u | ((r) * 0xff0000u) | ((g) * 0xff00u) | ((b) * 0xffu))
#define PX_INVERT(p) ((p) ^ 0x00ffffffu)
// beebjit's teletext blend: fg and bg are one unit per channel there (see
// teletext_init), so the multiply is per channel.
#define PX_BLEND(v, fg, bg) (((v) * (fg) + (255 - (v)) * (bg)) | 0xff000000u)
#define PX_COPY8(dst, src) memcpy((dst), (src), 8 * sizeof(render_px))
#define PX_COPY16(dst, src) memcpy((dst), (src), 16 * sizeof(render_px))
#endif

#endif
