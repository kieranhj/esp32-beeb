// esp32-beeb: cycle-stepped 6502 / 65C12 core.
//
// Plain C, no ESP headers: this file and cpu6502.c compile on a desktop
// compiler for trace diffs against beebjit.
//
// Every bus access is one emulated cycle and goes through one inline read or
// write, which first runs the per-cycle hook (CPU6502_TICK). The machine
// hangs the CRTC/ULA/VIA step off that hook.
//
// cpu->cmos = 1 gives the 65C12 in the Master 128 (full opcode map, CMOS bus
// quirks); 0 gives the NMOS 6502 with the documented opcodes only. Memory is
// a flat 64 KB for now; the Master paging replaces that with page tables.

#ifndef CPU6502_H
#define CPU6502_H

#include <stdint.h>

struct cpu6502 {
    uint16_t pc;
    uint8_t a, x, y, s;
    // Flags kept unpacked. n holds bit 7 only (0 or 0x80), the rest are 0/1.
    uint8_t n, z, c, v, i, d;
    uint8_t cmos;  // 1: 65C12 (Master), 0: NMOS 6502 (Model B)
    // Interrupt lines, sampled at instruction boundaries (Phase 1 moves the
    // sample point to the penultimate cycle of each instruction).
    uint8_t irq_line;
    uint8_t nmi_line, nmi_seen;
    uint32_t cyc;      // running cycle counter used by the hot loop, wraps
    uint64_t cycles;   // total, updated when cpu6502_run returns
    // Phase 0 memory: flat 64 KB. Phase 1 replaces this with page tables.
    uint8_t *mem;
    // Scratch for the per-cycle hook. Lives in the struct so that it is part
    // of the local copy cpu6502_run works on (see the aliasing note there).
    uint32_t hook[4];
};

// cpu6502.c is compiled through a wrapper that may define these first:
//   CPU6502_TICK(cpu)      per-cycle hook, default nothing
//   CPU6502_CODE_ATTR      attribute for the hot functions (IRAM_ATTR)
//   CPU6502_TABLE_ATTR     attribute for the dispatch table (DRAM_ATTR)
//   CPU6502_USE_SWITCH     dispatch with a switch instead of computed goto
//   CPU6502_FN(name)       renames the public entry points per variant
#ifndef CPU6502_FN
#define CPU6502_FN(name) name
#endif

void CPU6502_FN(cpu6502_reset)(struct cpu6502 *cpu);
// Runs whole instructions until cpu->cycles >= until. Returns cycles run.
uint64_t CPU6502_FN(cpu6502_run)(struct cpu6502 *cpu, uint64_t until);
// Packs/unpacks the flags to the P register layout (bit 5 set, B as given).
uint8_t CPU6502_FN(cpu6502_get_p)(const struct cpu6502 *cpu, int b);
void CPU6502_FN(cpu6502_set_p)(struct cpu6502 *cpu, uint8_t p);

#endif
