// esp32-beeb: cycle-stepped NMOS 6502 core. See cpu6502.h.
//
// Structure: one big switch per opcode; every bus access is an inline rd() or
// wr() that runs the per-cycle hook first. The addressing-mode helpers
// reproduce the real chip's bus sequence, including the dummy accesses, so
// that Phase 1 can hang cycle-exact peripherals off the hook without
// restructuring anything here.

#include "cpu6502.h"

#ifndef CPU6502_TICK
#define CPU6502_TICK(cpu) ((void)0)
#endif
#ifndef CPU6502_CODE_ATTR
#define CPU6502_CODE_ATTR
#endif
#ifndef CPU6502_TABLE_ATTR
#define CPU6502_TABLE_ATTR
#endif

#define INLINE static inline __attribute__((always_inline))

// ---- bus -------------------------------------------------------------------

INLINE uint8_t rd(struct cpu6502 *cpu, uint16_t addr)
{
    cpu->cyc++;
    CPU6502_TICK(cpu);
    return cpu->mem[addr];
}

INLINE void wr(struct cpu6502 *cpu, uint16_t addr, uint8_t v)
{
    cpu->cyc++;
    CPU6502_TICK(cpu);
    cpu->mem[addr] = v;
}

INLINE uint8_t fetch(struct cpu6502 *cpu)
{
    return rd(cpu, cpu->pc++);
}

INLINE void push(struct cpu6502 *cpu, uint8_t v)
{
    wr(cpu, 0x100 | cpu->s, v);
    cpu->s--;
}

INLINE uint8_t pull(struct cpu6502 *cpu)
{
    cpu->s++;
    return rd(cpu, 0x100 | cpu->s);
}

// ---- flags -----------------------------------------------------------------

INLINE void set_nz(struct cpu6502 *cpu, uint8_t v)
{
    cpu->n = v & 0x80;
    cpu->z = (v == 0);
}

INLINE uint8_t pack_p(const struct cpu6502 *cpu, int b)
{
    return (cpu->n ? 0x80 : 0) | (cpu->v ? 0x40 : 0) | 0x20 | (b ? 0x10 : 0) |
           (cpu->d ? 0x08 : 0) | (cpu->i ? 0x04 : 0) | (cpu->z ? 0x02 : 0) |
           (cpu->c ? 0x01 : 0);
}

INLINE void unpack_p(struct cpu6502 *cpu, uint8_t p)
{
    cpu->n = p & 0x80;
    cpu->v = (p >> 6) & 1;
    cpu->d = (p >> 3) & 1;
    cpu->i = (p >> 2) & 1;
    cpu->z = (p >> 1) & 1;
    cpu->c = p & 1;
}

uint8_t CPU6502_FN(cpu6502_get_p)(const struct cpu6502 *cpu, int b)
{
    return pack_p(cpu, b);
}

void CPU6502_FN(cpu6502_set_p)(struct cpu6502 *cpu, uint8_t p)
{
    unpack_p(cpu, p);
}

// ---- addressing modes: each returns the effective address having performed
// exactly the bus cycles the real chip does before the data cycle. ----------

INLINE uint16_t am_zp(struct cpu6502 *cpu)
{
    return fetch(cpu);
}

INLINE uint16_t am_zpi(struct cpu6502 *cpu, uint8_t idx)
{
    uint8_t zp = fetch(cpu);
    (void)rd(cpu, zp);  // dummy read of the unindexed zero page address
    return (uint8_t)(zp + idx);
}

INLINE uint16_t am_abs(struct cpu6502 *cpu)
{
    uint16_t lo = fetch(cpu);
    return lo | (fetch(cpu) << 8);
}

// Indexed absolute for reads: the extra cycle happens only on page crossing,
// and is a read from the un-carried address.
INLINE uint16_t am_absi_r(struct cpu6502 *cpu, uint8_t idx)
{
    uint16_t base = am_abs(cpu);
    uint16_t ea = base + idx;
    if ((ea ^ base) & 0xff00) {
        (void)rd(cpu, (base & 0xff00) | (ea & 0xff));
    }
    return ea;
}

// Indexed absolute for writes and RMW: the dummy read always happens.
INLINE uint16_t am_absi_w(struct cpu6502 *cpu, uint8_t idx)
{
    uint16_t base = am_abs(cpu);
    uint16_t ea = base + idx;
    (void)rd(cpu, (base & 0xff00) | (ea & 0xff));
    return ea;
}

INLINE uint16_t am_izx(struct cpu6502 *cpu)
{
    uint8_t zp = fetch(cpu);
    (void)rd(cpu, zp);
    zp += cpu->x;
    uint16_t lo = rd(cpu, zp);
    return lo | (rd(cpu, (uint8_t)(zp + 1)) << 8);
}

INLINE uint16_t am_izy_r(struct cpu6502 *cpu)
{
    uint8_t zp = fetch(cpu);
    uint16_t lo = rd(cpu, zp);
    uint16_t base = lo | (rd(cpu, (uint8_t)(zp + 1)) << 8);
    uint16_t ea = base + cpu->y;
    if ((ea ^ base) & 0xff00) {
        (void)rd(cpu, (base & 0xff00) | (ea & 0xff));
    }
    return ea;
}

INLINE uint16_t am_izy_w(struct cpu6502 *cpu)
{
    uint8_t zp = fetch(cpu);
    uint16_t lo = rd(cpu, zp);
    uint16_t base = lo | (rd(cpu, (uint8_t)(zp + 1)) << 8);
    uint16_t ea = base + cpu->y;
    (void)rd(cpu, (base & 0xff00) | (ea & 0xff));
    return ea;
}

// ---- ALU -------------------------------------------------------------------

INLINE void op_adc(struct cpu6502 *cpu, uint8_t m)
{
    uint8_t a = cpu->a;
    if (!cpu->d) {
        unsigned sum = a + m + cpu->c;
        cpu->v = ((a ^ sum) & (m ^ sum) & 0x80) != 0;
        cpu->c = sum > 0xff;
        cpu->a = (uint8_t)sum;
        set_nz(cpu, cpu->a);
    } else {
        // NMOS decimal: Z from the binary sum, N and V from the intermediate
        // high nibble before the final +0x60 adjust.
        unsigned bin = a + m + cpu->c;
        int al = (a & 0x0f) + (m & 0x0f) + cpu->c;
        if (al >= 0x0a) {
            al = ((al + 0x06) & 0x0f) + 0x10;
        }
        int r = (a & 0xf0) + (m & 0xf0) + al;
        int rs = (int8_t)(a & 0xf0) + (int8_t)(m & 0xf0) + al;
        cpu->n = r & 0x80;
        cpu->v = (rs < -128) || (rs > 127);
        if (r >= 0xa0) {
            r += 0x60;
        }
        cpu->c = r >= 0x100;
        cpu->z = ((bin & 0xff) == 0);
        cpu->a = (uint8_t)r;
    }
}

INLINE void op_sbc(struct cpu6502 *cpu, uint8_t m)
{
    uint8_t a = cpu->a;
    unsigned borrow = cpu->c ^ 1;
    int bin = a - m - borrow;
    uint8_t res = (uint8_t)bin;
    // Flags are always binary on NMOS SBC.
    cpu->v = ((a ^ m) & (a ^ res) & 0x80) != 0;
    cpu->c = bin >= 0;
    set_nz(cpu, res);
    if (!cpu->d) {
        cpu->a = res;
    } else {
        int al = (a & 0x0f) - (m & 0x0f) - borrow;
        if (al < 0) {
            al = ((al - 0x06) & 0x0f) - 0x10;
        }
        int r = (a & 0xf0) - (m & 0xf0) + al;
        if (r < 0) {
            r -= 0x60;
        }
        cpu->a = (uint8_t)r;
    }
}

INLINE void op_cmp(struct cpu6502 *cpu, uint8_t r, uint8_t m)
{
    cpu->c = r >= m;
    set_nz(cpu, (uint8_t)(r - m));
}

INLINE void op_bit(struct cpu6502 *cpu, uint8_t m)
{
    cpu->n = m & 0x80;
    cpu->v = (m >> 6) & 1;
    cpu->z = ((cpu->a & m) == 0);
}

INLINE uint8_t op_asl(struct cpu6502 *cpu, uint8_t m)
{
    cpu->c = m >> 7;
    m <<= 1;
    set_nz(cpu, m);
    return m;
}

INLINE uint8_t op_lsr(struct cpu6502 *cpu, uint8_t m)
{
    cpu->c = m & 1;
    m >>= 1;
    set_nz(cpu, m);
    return m;
}

INLINE uint8_t op_rol(struct cpu6502 *cpu, uint8_t m)
{
    uint8_t c = cpu->c;
    cpu->c = m >> 7;
    m = (uint8_t)((m << 1) | c);
    set_nz(cpu, m);
    return m;
}

INLINE uint8_t op_ror(struct cpu6502 *cpu, uint8_t m)
{
    uint8_t c = cpu->c;
    cpu->c = m & 1;
    m = (uint8_t)((m >> 1) | (c << 7));
    set_nz(cpu, m);
    return m;
}

// Read-modify-write: read, dummy write of the old value, write of the new.
#define RMW(ea, op)                              \
    do {                                         \
        uint16_t ea_ = (ea);                     \
        uint8_t m_ = rd(cpu, ea_);               \
        wr(cpu, ea_, m_);                        \
        wr(cpu, ea_, op(cpu, m_));               \
    } while (0)

INLINE void branch(struct cpu6502 *cpu, int taken)
{
    int8_t off = (int8_t)fetch(cpu);
    if (!taken) {
        return;
    }
    uint16_t old = cpu->pc;
    uint16_t npc = old + off;
    (void)rd(cpu, old);  // dummy read of the next opcode
    if ((npc ^ old) & 0xff00) {
        (void)rd(cpu, (old & 0xff00) | (npc & 0xff));
    }
    cpu->pc = npc;
}

// Interrupt entry: BRK and hardware interrupts share this sequence.
INLINE void interrupt(struct cpu6502 *cpu, uint16_t vec, int b)
{
    push(cpu, cpu->pc >> 8);
    push(cpu, cpu->pc & 0xff);
    push(cpu, pack_p(cpu, b));
    cpu->i = 1;
    uint16_t lo = rd(cpu, vec);
    cpu->pc = lo | (rd(cpu, vec + 1) << 8);
}

// Undocumented opcode: Phase 1 implements these. For Phase 0 treat them as
// one-byte NOPs so a stray byte does not derail a measurement.
INLINE void cpu6502_undoc(struct cpu6502 *cpu, uint8_t op)
{
    (void)op;
    (void)rd(cpu, cpu->pc);
}

void CPU6502_FN(cpu6502_reset)(struct cpu6502 *cpu)
{
    cpu->a = cpu->x = cpu->y = 0;
    cpu->s = 0xfd;
    cpu->n = cpu->z = cpu->c = cpu->v = cpu->d = 0;
    cpu->i = 1;
    cpu->irq_line = 0;
    cpu->nmi_line = cpu->nmi_seen = 0;
    cpu->pc = cpu->mem[0xfffc] | (cpu->mem[0xfffd] << 8);
}

CPU6502_CODE_ATTR
uint64_t CPU6502_FN(cpu6502_run)(struct cpu6502 *cpu_in, uint64_t until)
{
    // Work on a local copy. A byte store through cpu->mem could alias any
    // field of *cpu_in, so with the pointer the compiler reloads every
    // register after every bus write (measured: about 30 clocks per cycle on
    // the ESP32-S3). A local whose address never escapes cannot be aliased by
    // mem[], so its fields stay in machine registers.
    struct cpu6502 local = *cpu_in;
    struct cpu6502 *cpu = &local;
    // The hot loop counts in 32 bits (cpu->cyc); the 64-bit total is folded
    // in on exit. A slice is never anywhere near 2^31 cycles.
    uint32_t start = cpu->cyc;
    uint32_t end = start + (uint32_t)(until - cpu->cycles);

#ifdef CPU6502_USE_SWITCH
#define OPC(h) case 0x##h:
#define OPC_DEFAULT default:
#define NEXT break
#define DISPATCH_END }
#else
    // Computed-goto dispatch: one load and one indirect jump per opcode.
    // GCC for Xtensa turned the equivalent switch into a compare tree, which
    // Phase 0 measured as noticeably slower. Opcodes without a handler go to
    // op_undoc; Phase 1 fills the table in. The table lives wherever
    // CPU6502_TABLE_ATTR says (DRAM on the ESP32-S3).
    static CPU6502_TABLE_ATTR void *const dispatch[256] = {
        &&op_00, &&op_01, &&op_undoc, &&op_undoc, &&op_undoc, &&op_05, &&op_06, &&op_undoc,
        &&op_08, &&op_09, &&op_0a, &&op_undoc, &&op_undoc, &&op_0d, &&op_0e, &&op_undoc,
        &&op_10, &&op_11, &&op_undoc, &&op_undoc, &&op_undoc, &&op_15, &&op_16, &&op_undoc,
        &&op_18, &&op_19, &&op_undoc, &&op_undoc, &&op_undoc, &&op_1d, &&op_1e, &&op_undoc,
        &&op_20, &&op_21, &&op_undoc, &&op_undoc, &&op_24, &&op_25, &&op_26, &&op_undoc,
        &&op_28, &&op_29, &&op_2a, &&op_undoc, &&op_2c, &&op_2d, &&op_2e, &&op_undoc,
        &&op_30, &&op_31, &&op_undoc, &&op_undoc, &&op_undoc, &&op_35, &&op_36, &&op_undoc,
        &&op_38, &&op_39, &&op_undoc, &&op_undoc, &&op_undoc, &&op_3d, &&op_3e, &&op_undoc,
        &&op_40, &&op_41, &&op_undoc, &&op_undoc, &&op_undoc, &&op_45, &&op_46, &&op_undoc,
        &&op_48, &&op_49, &&op_4a, &&op_undoc, &&op_4c, &&op_4d, &&op_4e, &&op_undoc,
        &&op_50, &&op_51, &&op_undoc, &&op_undoc, &&op_undoc, &&op_55, &&op_56, &&op_undoc,
        &&op_58, &&op_59, &&op_undoc, &&op_undoc, &&op_undoc, &&op_5d, &&op_5e, &&op_undoc,
        &&op_60, &&op_61, &&op_undoc, &&op_undoc, &&op_undoc, &&op_65, &&op_66, &&op_undoc,
        &&op_68, &&op_69, &&op_6a, &&op_undoc, &&op_6c, &&op_6d, &&op_6e, &&op_undoc,
        &&op_70, &&op_71, &&op_undoc, &&op_undoc, &&op_undoc, &&op_75, &&op_76, &&op_undoc,
        &&op_78, &&op_79, &&op_undoc, &&op_undoc, &&op_undoc, &&op_7d, &&op_7e, &&op_undoc,
        &&op_undoc, &&op_81, &&op_undoc, &&op_undoc, &&op_84, &&op_85, &&op_86, &&op_undoc,
        &&op_88, &&op_undoc, &&op_8a, &&op_undoc, &&op_8c, &&op_8d, &&op_8e, &&op_undoc,
        &&op_90, &&op_91, &&op_undoc, &&op_undoc, &&op_94, &&op_95, &&op_96, &&op_undoc,
        &&op_98, &&op_99, &&op_9a, &&op_undoc, &&op_undoc, &&op_9d, &&op_undoc, &&op_undoc,
        &&op_a0, &&op_a1, &&op_a2, &&op_undoc, &&op_a4, &&op_a5, &&op_a6, &&op_undoc,
        &&op_a8, &&op_a9, &&op_aa, &&op_undoc, &&op_ac, &&op_ad, &&op_ae, &&op_undoc,
        &&op_b0, &&op_b1, &&op_undoc, &&op_undoc, &&op_b4, &&op_b5, &&op_b6, &&op_undoc,
        &&op_b8, &&op_b9, &&op_ba, &&op_undoc, &&op_bc, &&op_bd, &&op_be, &&op_undoc,
        &&op_c0, &&op_c1, &&op_undoc, &&op_undoc, &&op_c4, &&op_c5, &&op_c6, &&op_undoc,
        &&op_c8, &&op_c9, &&op_ca, &&op_undoc, &&op_cc, &&op_cd, &&op_ce, &&op_undoc,
        &&op_d0, &&op_d1, &&op_undoc, &&op_undoc, &&op_undoc, &&op_d5, &&op_d6, &&op_undoc,
        &&op_d8, &&op_d9, &&op_undoc, &&op_undoc, &&op_undoc, &&op_dd, &&op_de, &&op_undoc,
        &&op_e0, &&op_e1, &&op_undoc, &&op_undoc, &&op_e4, &&op_e5, &&op_e6, &&op_undoc,
        &&op_e8, &&op_e9, &&op_ea, &&op_undoc, &&op_ec, &&op_ed, &&op_ee, &&op_undoc,
        &&op_f0, &&op_f1, &&op_undoc, &&op_undoc, &&op_undoc, &&op_f5, &&op_f6, &&op_undoc,
        &&op_f8, &&op_f9, &&op_undoc, &&op_undoc, &&op_undoc, &&op_fd, &&op_fe, &&op_undoc,
    };
#define OPC(h) op_##h:
#define OPC_DEFAULT op_undoc:
#define NEXT continue
#define DISPATCH_END
#endif

    while ((int32_t)(end - cpu->cyc) > 0) {
        // Interrupt lines sampled at the instruction boundary.
        if (cpu->nmi_line && !cpu->nmi_seen) {
            cpu->nmi_seen = 1;
            (void)rd(cpu, cpu->pc);
            (void)rd(cpu, cpu->pc);
            interrupt(cpu, 0xfffa, 0);
            continue;
        }
        if (cpu->irq_line && !cpu->i) {
            (void)rd(cpu, cpu->pc);
            (void)rd(cpu, cpu->pc);
            interrupt(cpu, 0xfffe, 0);
            continue;
        }

        uint8_t op = fetch(cpu);
        uint16_t ea;
        uint8_t t;

#ifdef CPU6502_USE_SWITCH
        switch (op) {
#else
        goto *dispatch[op];
#endif
        // ---- loads ----
        OPC(a9) set_nz(cpu, cpu->a = fetch(cpu)); NEXT;
        OPC(a5) set_nz(cpu, cpu->a = rd(cpu, am_zp(cpu))); NEXT;
        OPC(b5) set_nz(cpu, cpu->a = rd(cpu, am_zpi(cpu, cpu->x))); NEXT;
        OPC(ad) set_nz(cpu, cpu->a = rd(cpu, am_abs(cpu))); NEXT;
        OPC(bd) set_nz(cpu, cpu->a = rd(cpu, am_absi_r(cpu, cpu->x))); NEXT;
        OPC(b9) set_nz(cpu, cpu->a = rd(cpu, am_absi_r(cpu, cpu->y))); NEXT;
        OPC(a1) set_nz(cpu, cpu->a = rd(cpu, am_izx(cpu))); NEXT;
        OPC(b1) set_nz(cpu, cpu->a = rd(cpu, am_izy_r(cpu))); NEXT;

        OPC(a2) set_nz(cpu, cpu->x = fetch(cpu)); NEXT;
        OPC(a6) set_nz(cpu, cpu->x = rd(cpu, am_zp(cpu))); NEXT;
        OPC(b6) set_nz(cpu, cpu->x = rd(cpu, am_zpi(cpu, cpu->y))); NEXT;
        OPC(ae) set_nz(cpu, cpu->x = rd(cpu, am_abs(cpu))); NEXT;
        OPC(be) set_nz(cpu, cpu->x = rd(cpu, am_absi_r(cpu, cpu->y))); NEXT;

        OPC(a0) set_nz(cpu, cpu->y = fetch(cpu)); NEXT;
        OPC(a4) set_nz(cpu, cpu->y = rd(cpu, am_zp(cpu))); NEXT;
        OPC(b4) set_nz(cpu, cpu->y = rd(cpu, am_zpi(cpu, cpu->x))); NEXT;
        OPC(ac) set_nz(cpu, cpu->y = rd(cpu, am_abs(cpu))); NEXT;
        OPC(bc) set_nz(cpu, cpu->y = rd(cpu, am_absi_r(cpu, cpu->x))); NEXT;

        // ---- stores ----
        OPC(85) wr(cpu, am_zp(cpu), cpu->a); NEXT;
        OPC(95) wr(cpu, am_zpi(cpu, cpu->x), cpu->a); NEXT;
        OPC(8d) wr(cpu, am_abs(cpu), cpu->a); NEXT;
        OPC(9d) wr(cpu, am_absi_w(cpu, cpu->x), cpu->a); NEXT;
        OPC(99) wr(cpu, am_absi_w(cpu, cpu->y), cpu->a); NEXT;
        OPC(81) wr(cpu, am_izx(cpu), cpu->a); NEXT;
        OPC(91) wr(cpu, am_izy_w(cpu), cpu->a); NEXT;

        OPC(86) wr(cpu, am_zp(cpu), cpu->x); NEXT;
        OPC(96) wr(cpu, am_zpi(cpu, cpu->y), cpu->x); NEXT;
        OPC(8e) wr(cpu, am_abs(cpu), cpu->x); NEXT;

        OPC(84) wr(cpu, am_zp(cpu), cpu->y); NEXT;
        OPC(94) wr(cpu, am_zpi(cpu, cpu->x), cpu->y); NEXT;
        OPC(8c) wr(cpu, am_abs(cpu), cpu->y); NEXT;

        // ---- transfers ----
        OPC(aa) (void)rd(cpu, cpu->pc); set_nz(cpu, cpu->x = cpu->a); NEXT;
        OPC(a8) (void)rd(cpu, cpu->pc); set_nz(cpu, cpu->y = cpu->a); NEXT;
        OPC(8a) (void)rd(cpu, cpu->pc); set_nz(cpu, cpu->a = cpu->x); NEXT;
        OPC(98) (void)rd(cpu, cpu->pc); set_nz(cpu, cpu->a = cpu->y); NEXT;
        OPC(ba) (void)rd(cpu, cpu->pc); set_nz(cpu, cpu->x = cpu->s); NEXT;
        OPC(9a) (void)rd(cpu, cpu->pc); cpu->s = cpu->x; NEXT;

        // ---- stack ----
        OPC(48) (void)rd(cpu, cpu->pc); push(cpu, cpu->a); NEXT;
        OPC(08) (void)rd(cpu, cpu->pc); push(cpu, pack_p(cpu, 1)); NEXT;
        OPC(68)
            (void)rd(cpu, cpu->pc);
            (void)rd(cpu, 0x100 | cpu->s);
            set_nz(cpu, cpu->a = pull(cpu));
            NEXT;
        OPC(28)
            (void)rd(cpu, cpu->pc);
            (void)rd(cpu, 0x100 | cpu->s);
            unpack_p(cpu, pull(cpu));
            NEXT;

        // ---- ALU ----
        OPC(69) op_adc(cpu, fetch(cpu)); NEXT;
        OPC(65) op_adc(cpu, rd(cpu, am_zp(cpu))); NEXT;
        OPC(75) op_adc(cpu, rd(cpu, am_zpi(cpu, cpu->x))); NEXT;
        OPC(6d) op_adc(cpu, rd(cpu, am_abs(cpu))); NEXT;
        OPC(7d) op_adc(cpu, rd(cpu, am_absi_r(cpu, cpu->x))); NEXT;
        OPC(79) op_adc(cpu, rd(cpu, am_absi_r(cpu, cpu->y))); NEXT;
        OPC(61) op_adc(cpu, rd(cpu, am_izx(cpu))); NEXT;
        OPC(71) op_adc(cpu, rd(cpu, am_izy_r(cpu))); NEXT;

        OPC(e9) op_sbc(cpu, fetch(cpu)); NEXT;
        OPC(e5) op_sbc(cpu, rd(cpu, am_zp(cpu))); NEXT;
        OPC(f5) op_sbc(cpu, rd(cpu, am_zpi(cpu, cpu->x))); NEXT;
        OPC(ed) op_sbc(cpu, rd(cpu, am_abs(cpu))); NEXT;
        OPC(fd) op_sbc(cpu, rd(cpu, am_absi_r(cpu, cpu->x))); NEXT;
        OPC(f9) op_sbc(cpu, rd(cpu, am_absi_r(cpu, cpu->y))); NEXT;
        OPC(e1) op_sbc(cpu, rd(cpu, am_izx(cpu))); NEXT;
        OPC(f1) op_sbc(cpu, rd(cpu, am_izy_r(cpu))); NEXT;

        OPC(29) set_nz(cpu, cpu->a &= fetch(cpu)); NEXT;
        OPC(25) set_nz(cpu, cpu->a &= rd(cpu, am_zp(cpu))); NEXT;
        OPC(35) set_nz(cpu, cpu->a &= rd(cpu, am_zpi(cpu, cpu->x))); NEXT;
        OPC(2d) set_nz(cpu, cpu->a &= rd(cpu, am_abs(cpu))); NEXT;
        OPC(3d) set_nz(cpu, cpu->a &= rd(cpu, am_absi_r(cpu, cpu->x))); NEXT;
        OPC(39) set_nz(cpu, cpu->a &= rd(cpu, am_absi_r(cpu, cpu->y))); NEXT;
        OPC(21) set_nz(cpu, cpu->a &= rd(cpu, am_izx(cpu))); NEXT;
        OPC(31) set_nz(cpu, cpu->a &= rd(cpu, am_izy_r(cpu))); NEXT;

        OPC(09) set_nz(cpu, cpu->a |= fetch(cpu)); NEXT;
        OPC(05) set_nz(cpu, cpu->a |= rd(cpu, am_zp(cpu))); NEXT;
        OPC(15) set_nz(cpu, cpu->a |= rd(cpu, am_zpi(cpu, cpu->x))); NEXT;
        OPC(0d) set_nz(cpu, cpu->a |= rd(cpu, am_abs(cpu))); NEXT;
        OPC(1d) set_nz(cpu, cpu->a |= rd(cpu, am_absi_r(cpu, cpu->x))); NEXT;
        OPC(19) set_nz(cpu, cpu->a |= rd(cpu, am_absi_r(cpu, cpu->y))); NEXT;
        OPC(01) set_nz(cpu, cpu->a |= rd(cpu, am_izx(cpu))); NEXT;
        OPC(11) set_nz(cpu, cpu->a |= rd(cpu, am_izy_r(cpu))); NEXT;

        OPC(49) set_nz(cpu, cpu->a ^= fetch(cpu)); NEXT;
        OPC(45) set_nz(cpu, cpu->a ^= rd(cpu, am_zp(cpu))); NEXT;
        OPC(55) set_nz(cpu, cpu->a ^= rd(cpu, am_zpi(cpu, cpu->x))); NEXT;
        OPC(4d) set_nz(cpu, cpu->a ^= rd(cpu, am_abs(cpu))); NEXT;
        OPC(5d) set_nz(cpu, cpu->a ^= rd(cpu, am_absi_r(cpu, cpu->x))); NEXT;
        OPC(59) set_nz(cpu, cpu->a ^= rd(cpu, am_absi_r(cpu, cpu->y))); NEXT;
        OPC(41) set_nz(cpu, cpu->a ^= rd(cpu, am_izx(cpu))); NEXT;
        OPC(51) set_nz(cpu, cpu->a ^= rd(cpu, am_izy_r(cpu))); NEXT;

        OPC(c9) op_cmp(cpu, cpu->a, fetch(cpu)); NEXT;
        OPC(c5) op_cmp(cpu, cpu->a, rd(cpu, am_zp(cpu))); NEXT;
        OPC(d5) op_cmp(cpu, cpu->a, rd(cpu, am_zpi(cpu, cpu->x))); NEXT;
        OPC(cd) op_cmp(cpu, cpu->a, rd(cpu, am_abs(cpu))); NEXT;
        OPC(dd) op_cmp(cpu, cpu->a, rd(cpu, am_absi_r(cpu, cpu->x))); NEXT;
        OPC(d9) op_cmp(cpu, cpu->a, rd(cpu, am_absi_r(cpu, cpu->y))); NEXT;
        OPC(c1) op_cmp(cpu, cpu->a, rd(cpu, am_izx(cpu))); NEXT;
        OPC(d1) op_cmp(cpu, cpu->a, rd(cpu, am_izy_r(cpu))); NEXT;

        OPC(e0) op_cmp(cpu, cpu->x, fetch(cpu)); NEXT;
        OPC(e4) op_cmp(cpu, cpu->x, rd(cpu, am_zp(cpu))); NEXT;
        OPC(ec) op_cmp(cpu, cpu->x, rd(cpu, am_abs(cpu))); NEXT;
        OPC(c0) op_cmp(cpu, cpu->y, fetch(cpu)); NEXT;
        OPC(c4) op_cmp(cpu, cpu->y, rd(cpu, am_zp(cpu))); NEXT;
        OPC(cc) op_cmp(cpu, cpu->y, rd(cpu, am_abs(cpu))); NEXT;

        OPC(24) op_bit(cpu, rd(cpu, am_zp(cpu))); NEXT;
        OPC(2c) op_bit(cpu, rd(cpu, am_abs(cpu))); NEXT;

        // ---- shifts ----
        OPC(0a) (void)rd(cpu, cpu->pc); cpu->a = op_asl(cpu, cpu->a); NEXT;
        OPC(06) RMW(am_zp(cpu), op_asl); NEXT;
        OPC(16) RMW(am_zpi(cpu, cpu->x), op_asl); NEXT;
        OPC(0e) RMW(am_abs(cpu), op_asl); NEXT;
        OPC(1e) RMW(am_absi_w(cpu, cpu->x), op_asl); NEXT;

        OPC(4a) (void)rd(cpu, cpu->pc); cpu->a = op_lsr(cpu, cpu->a); NEXT;
        OPC(46) RMW(am_zp(cpu), op_lsr); NEXT;
        OPC(56) RMW(am_zpi(cpu, cpu->x), op_lsr); NEXT;
        OPC(4e) RMW(am_abs(cpu), op_lsr); NEXT;
        OPC(5e) RMW(am_absi_w(cpu, cpu->x), op_lsr); NEXT;

        OPC(2a) (void)rd(cpu, cpu->pc); cpu->a = op_rol(cpu, cpu->a); NEXT;
        OPC(26) RMW(am_zp(cpu), op_rol); NEXT;
        OPC(36) RMW(am_zpi(cpu, cpu->x), op_rol); NEXT;
        OPC(2e) RMW(am_abs(cpu), op_rol); NEXT;
        OPC(3e) RMW(am_absi_w(cpu, cpu->x), op_rol); NEXT;

        OPC(6a) (void)rd(cpu, cpu->pc); cpu->a = op_ror(cpu, cpu->a); NEXT;
        OPC(66) RMW(am_zp(cpu), op_ror); NEXT;
        OPC(76) RMW(am_zpi(cpu, cpu->x), op_ror); NEXT;
        OPC(6e) RMW(am_abs(cpu), op_ror); NEXT;
        OPC(7e) RMW(am_absi_w(cpu, cpu->x), op_ror); NEXT;

        // ---- inc/dec ----
#define op_inc(cpu, m) ((void)set_nz(cpu, (uint8_t)((m) + 1)), (uint8_t)((m) + 1))
#define op_dec(cpu, m) ((void)set_nz(cpu, (uint8_t)((m) - 1)), (uint8_t)((m) - 1))
        OPC(e6) RMW(am_zp(cpu), op_inc); NEXT;
        OPC(f6) RMW(am_zpi(cpu, cpu->x), op_inc); NEXT;
        OPC(ee) RMW(am_abs(cpu), op_inc); NEXT;
        OPC(fe) RMW(am_absi_w(cpu, cpu->x), op_inc); NEXT;
        OPC(c6) RMW(am_zp(cpu), op_dec); NEXT;
        OPC(d6) RMW(am_zpi(cpu, cpu->x), op_dec); NEXT;
        OPC(ce) RMW(am_abs(cpu), op_dec); NEXT;
        OPC(de) RMW(am_absi_w(cpu, cpu->x), op_dec); NEXT;
        OPC(e8) (void)rd(cpu, cpu->pc); set_nz(cpu, ++cpu->x); NEXT;
        OPC(ca) (void)rd(cpu, cpu->pc); set_nz(cpu, --cpu->x); NEXT;
        OPC(c8) (void)rd(cpu, cpu->pc); set_nz(cpu, ++cpu->y); NEXT;
        OPC(88) (void)rd(cpu, cpu->pc); set_nz(cpu, --cpu->y); NEXT;

        // ---- flags ----
        OPC(18) (void)rd(cpu, cpu->pc); cpu->c = 0; NEXT;
        OPC(38) (void)rd(cpu, cpu->pc); cpu->c = 1; NEXT;
        OPC(58) (void)rd(cpu, cpu->pc); cpu->i = 0; NEXT;
        OPC(78) (void)rd(cpu, cpu->pc); cpu->i = 1; NEXT;
        OPC(b8) (void)rd(cpu, cpu->pc); cpu->v = 0; NEXT;
        OPC(d8) (void)rd(cpu, cpu->pc); cpu->d = 0; NEXT;
        OPC(f8) (void)rd(cpu, cpu->pc); cpu->d = 1; NEXT;
        OPC(ea) (void)rd(cpu, cpu->pc); NEXT;

        // ---- branches ----
        OPC(10) branch(cpu, !cpu->n); NEXT;
        OPC(30) branch(cpu, cpu->n); NEXT;
        OPC(50) branch(cpu, !cpu->v); NEXT;
        OPC(70) branch(cpu, cpu->v); NEXT;
        OPC(90) branch(cpu, !cpu->c); NEXT;
        OPC(b0) branch(cpu, cpu->c); NEXT;
        OPC(d0) branch(cpu, !cpu->z); NEXT;
        OPC(f0) branch(cpu, cpu->z); NEXT;

        // ---- jumps and calls ----
        OPC(4c) cpu->pc = am_abs(cpu); NEXT;
        OPC(6c)
            ea = am_abs(cpu);
            t = rd(cpu, ea);
            // NMOS bug: the high byte read does not carry into the page.
            cpu->pc = t | (rd(cpu, (ea & 0xff00) | ((ea + 1) & 0xff)) << 8);
            NEXT;
        OPC(20)
            t = fetch(cpu);
            (void)rd(cpu, 0x100 | cpu->s);
            push(cpu, cpu->pc >> 8);
            push(cpu, cpu->pc & 0xff);
            cpu->pc = t | (fetch(cpu) << 8);
            NEXT;
        OPC(60)
            (void)rd(cpu, cpu->pc);
            (void)rd(cpu, 0x100 | cpu->s);
            t = pull(cpu);
            cpu->pc = t | (pull(cpu) << 8);
            (void)rd(cpu, cpu->pc);
            cpu->pc++;
            NEXT;
        OPC(40)
            (void)rd(cpu, cpu->pc);
            (void)rd(cpu, 0x100 | cpu->s);
            unpack_p(cpu, pull(cpu));
            t = pull(cpu);
            cpu->pc = t | (pull(cpu) << 8);
            NEXT;
        OPC(00)
            (void)fetch(cpu);  // padding byte
            interrupt(cpu, 0xfffe, 1);
            NEXT;

        OPC_DEFAULT
            cpu6502_undoc(cpu, op);
            NEXT;
        DISPATCH_END
    }
    uint32_t ran = cpu->cyc - start;
    cpu->cycles += ran;
    *cpu_in = local;
    return ran;
}
