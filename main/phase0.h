// Phase 0 benchmark glue shared by main.c and the three core variants.
#ifndef PHASE0_H
#define PHASE0_H
#include <stdint.h>
#include "cpu6502.h"

extern uint8_t phase0_ram[65536];
extern uint8_t *phase0_table[256];
extern uint32_t phase0_linebuf[256];
extern uint32_t phase0_expand[16];

// Representative video step, run once per emulated cycle by the "video"
// variant. What a 2 MHz-mode ULA step costs: advance the character counter,
// fetch the display byte from RAM, expand 8 pixels (as two 32-bit words of 4
// pixels at 8bpp) into a line buffer, count lines. State is cpu->hook[]:
// [0] memory address, [1] horizontal counter, [2] line count.
static inline __attribute__((always_inline)) void phase0_video_tick(struct cpu6502 *cpu)
{
    uint8_t b = phase0_ram[cpu->hook[0] & 0x7fff];
    cpu->hook[0]++;
    uint32_t *p = &phase0_linebuf[(cpu->hook[1] & 127) * 2];
    p[0] = phase0_expand[b >> 4];
    p[1] = phase0_expand[b & 15];
    if (++cpu->hook[1] == 128) {
        cpu->hook[1] = 0;
        cpu->hook[2]++;
    }
}

#define DECLARE_VARIANT(suffix)                                              \
    void cpu6502_reset_##suffix(struct cpu6502 *cpu);                        \
    uint64_t cpu6502_run_##suffix(struct cpu6502 *cpu, uint64_t until);
DECLARE_VARIANT(iram)
DECLARE_VARIANT(video)
DECLARE_VARIANT(flash)
DECLARE_VARIANT(flashv)
DECLARE_VARIANT(iramft)
#endif
