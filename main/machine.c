// esp32-beeb: BBC Master 128 machine. See machine.h.
//
// Timing model. The core calls machine_tick() once per 2 MHz cycle. The CRTC
// steps every cycle; the VIAs and ADC step on odd cycle counts (beebjit's
// VIA timers expire one tick after the 1 MHz boundary). A read or write of a
// 1 MHz address is stretched the way the real bus does it (beebjit's rule,
// confirmed on hardware): with the cycle count c before the access, the CPU
// idles for one cycle if c is even or two if it is odd, performs the access
// at the odd count with that count's 1 MHz step already applied, then the
// data cycle completes. 2 MHz hardware is "tick then access", except the
// video ULA at 0xFE20-0xFE23 which is "access then tick".

#include <stddef.h>
#include <string.h>

#include "machine.h"

#ifndef MACHINE_CODE_ATTR
#define MACHINE_CODE_ATTR
#endif

#include "via.c"

#define INLINE static inline __attribute__((always_inline))

struct machine;
static uint8_t machine_display_fetch(struct machine *m, uint16_t ma, uint8_t ra, int odd);
static void machine_vsync(struct machine *m, int level);
#define CRTC_FETCH(c, ma, ra, odd) machine_display_fetch(crtc_owner(c), ma, ra, odd)
#define CRTC_VSYNC(c, level) machine_vsync(crtc_owner(c), level)
static struct machine *crtc_owner(struct crtc *c);
#include "crtc.c"

// ---- paging -----------------------------------------------------------------

static void map_range(struct machine *m, int set, int first, int count,
                      uint8_t *rd_base, uint8_t *wr_base)
{
    for (int i = 0; i < count; i++) {
        m->rd_tab[set][first + i] = rd_base ? rd_base + i * 256 : 0;
        // A NULL write base means "ROM": every page writes to the bitbucket.
        m->wr_tab[set][first + i] = wr_base ? wr_base + i * 256 : m->bitbucket;
    }
}

static void map_range_const(struct machine *m, int set, int first, int count,
                            const uint8_t *rd_base, int stride)
{
    for (int i = 0; i < count; i++) {
        m->rd_tab[set][first + i] = (uint8_t *)rd_base + i * stride;
        m->wr_tab[set][first + i] = m->bitbucket;
    }
}

// 0x3000-0x7FFF: main RAM or LYNNE, separately for user code and for code
// running in the MOS VDU driver region. beebjit's rule (from b2): user code
// sees LYNNE iff X; MOS code sees LYNNE iff (HAZEL and X) or (not HAZEL and
// E). The surprising case HAZEL=0, X=1, E=0 sends MOS code to main RAM.
static void remap_shadow(struct machine *m)
{
    int x = (m->acccon >> 2) & 1;
    int e = (m->acccon >> 1) & 1;
    int hazel = (m->acccon >> 3) & 1;
    int mos_shadow = (hazel && x) || (!hazel && e);
    uint8_t *usr = x ? m->lynne : m->ram + 0x3000;
    uint8_t *mos = mos_shadow ? m->lynne : m->ram + 0x3000;
    map_range(m, 0, 0x30, 0x50, usr, usr);
    map_range(m, 1, 0x30, 0x50, mos, mos);
}

static void remap_sideways(struct machine *m)
{
    int bank = m->romsel & 0x0f;
    uint8_t *swram = 0;
    const uint8_t *rom = m->rom[bank];
    if (bank >= 4 && bank < 4 + MACHINE_SWRAM_SLOTS) {
        swram = m->swram[bank - 4];
    }
    for (int set = 0; set < 2; set++) {
        if (swram) {
            map_range(m, set, 0x80, 0x40, swram, swram);
        } else if (rom) {
            map_range_const(m, set, 0x80, 0x40, rom, 256);
        } else {
            map_range_const(m, set, 0x80, 0x40, m->empty_rom, 0);
        }
        if (m->romsel & 0x80) {
            map_range(m, set, 0x80, 0x10, m->andy, m->andy);
        }
    }
}

static void remap_hazel(struct machine *m)
{
    for (int set = 0; set < 2; set++) {
        if (m->acccon & 0x08) {
            map_range(m, set, 0xc0, 0x20, m->hazel, m->hazel);
        } else {
            map_range_const(m, set, 0xc0, 0x20, m->mos, 256);
        }
    }
}

static void remap_all(struct machine *m)
{
    for (int set = 0; set < 2; set++) {
        map_range(m, set, 0x00, 0x30, m->ram, m->ram);
        map_range_const(m, set, 0xe0, 0x1c, m->mos + 0x2000, 256);
        map_range(m, set, 0xfc, 0x03, 0, 0);
        m->wr_tab[set][0xfc] = m->wr_tab[set][0xfd] = m->wr_tab[set][0xfe] = 0;
        map_range_const(m, set, 0xff, 0x01, m->mos + 0x3f00, 256);
    }
    remap_shadow(m);
    remap_sideways(m);
    remap_hazel(m);
}

// ---- system VIA peripherals: keyboard, IC32 latch, CMOS, sound -------------

static int key_down(const struct machine *m, int row, int col)
{
    return (m->keys[col & 0xf] >> (row & 7)) & 1;
}

static int key_column_down(const struct machine *m, int col)
{
    return m->keys[col & 0xf] != 0;
}

static int any_key_down(const struct machine *m)
{
    for (int i = 0; i < 16; i++) {
        if (m->keys[i]) {
            return 1;
        }
    }
    return 0;
}

static uint8_t cmos_bus_value(const struct machine *m)
{
    if (m->cmos_enabled && !m->cmos_strobe && m->cmos_data && m->cmos_read) {
        return m->cmos_ram[m->cmos_addr & 0x3f];
    }
    return 0xff;
}

// Recomputes port A pin levels: keyboard bit 7, CMOS data, and CA2.
static void sysvia_port_a(struct machine *m)
{
    struct via *via = &m->sysvia;
    uint8_t bus = via_port_a_out(via);
    uint8_t periph = 0xff;
    int fire = 0;

    periph &= cmos_bus_value(m);
    bus &= periph;

    if (!(m->ic32 & 0x08)) {
        int row = (bus >> 4) & 7;
        int col = bus & 0xf;
        if (!key_down(m, row, col)) {
            periph &= 0x7f;
        }
        if (key_column_down(m, col)) {
            fire = 1;
        }
    } else if (any_key_down(m)) {
        via_set_ca2(via, 0);  // auto-scan toggles CA2 as it sweeps
        fire = 1;
    }
    via->peripheral_a = periph;
    via_update_bus_a(via);
    via_set_ca2(via, fire);
}

static void sysvia_port_b(struct machine *m)
{
    struct via *via = &m->sysvia;
    uint8_t bus = via_port_b(via);
    uint8_t bit = 1 << (bus & 7);
    if (bus & 0x08) {
        m->ic32 |= bit;
    } else {
        m->ic32 &= ~bit;
    }
    // IC32 bits 4,5: hardware scroll wrap size (beebjit's table).
    {
        static const uint16_t wrap[4] = {0x4000, 0x2000, 0x5000, 0x2800};
        m->crtc.screen_wrap_add = wrap[(m->ic32 >> 4) & 3];
    }

    // CMOS/RTC control: PB6 enable, PB7 address strobe, IC32 bit 1 read,
    // bit 2 data (beebjit's cmos.c).
    m->cmos_enabled = (bus >> 6) & 1;
    if (m->cmos_enabled) {
        int strobe = (bus >> 7) & 1;
        int data = (m->ic32 >> 2) & 1;
        int read = (m->ic32 >> 1) & 1;
        if (!strobe && m->cmos_strobe) {
            m->cmos_addr = via->bus_a & 0x3f;
        }
        m->cmos_strobe = strobe;
        m->cmos_data = data;
        m->cmos_read = read;
        // Writes (data high to low in write mode) are ignored for now.
    }
    sysvia_port_a(m);
}

static void sysvia_port_cb(struct via *via, void *ctx, int which)
{
    (void)via;
    struct machine *m = ctx;
    if (which == 1) {
        sysvia_port_b(m);
    } else {
        sysvia_port_a(m);
    }
}

// ---- per-cycle hook ---------------------------------------------------------

INLINE void machine_update_irq(struct machine *m, struct cpu6502 *cpu)
{
    cpu->irq_line = m->sysvia.irq | m->usrvia.irq;
}

// ---- display memory ---------------------------------------------------------

static struct machine *crtc_owner(struct crtc *c)
{
    return (struct machine *)((char *)c - offsetof(struct machine, crtc));
}

static void machine_vsync(struct machine *m, int level)
{
    via_set_ca1(&m->sysvia, level);
}

// beebjit's video_read_data_byte for the Master: MODE 7 style addressing
// when MA13 is set, else MA*8 + RA with the IC32 wrap; display comes from
// LYNNE when ACCCON D xor X, with ANDY and HAZEL visible below 0x3000 when
// paged in and D set.
static uint8_t machine_display_fetch(struct machine *m, uint16_t ma, uint8_t ra, int odd)
{
    uint32_t addr;
    if (ma & 0x2000) {
        addr = 0x7c00 | (ma & 0x3ff);
        if (!m->crtc.ula_teletext && m->crtc.ula_clock_fast && !odd) {
            addr ^= 64;  // 2 MHz bitmap fetch through the MODE 7 path
        }
    } else {
        addr = (uint32_t)ma * 8 + (ra & 7);
        if (ma & 0x1000) {
            addr -= m->crtc.screen_wrap_add;
        }
        addr &= 0x7fff;
    }
    int shadow = ((m->acccon & 1) ^ ((m->acccon >> 2) & 1)) != 0;
    if (addr >= 0x3000) {
        return shadow ? m->lynne[addr - 0x3000] : m->ram[addr];
    }
    if (m->acccon & 1) {
        if (addr < 0x1000) {
            if (m->romsel & 0x80) {
                return m->andy[addr];
            }
        } else if (m->acccon & 0x08) {
            return m->hazel[addr - 0x1000];
        }
    }
    return m->ram[addr];
}

INLINE void machine_tick(struct cpu6502 *cpu)
{
    struct machine *m = cpu->m;
    crtc_tick(&m->crtc, cpu->cyc - 1);
    if (m->adc_busy && --m->adc_countdown == 0) {
        m->adc_busy = 0;
        m->adc_ready = 1;
        via_set_cb1(&m->sysvia, 0);
    }
    // The VIAs step on odd cycle counts: beebjit's VIA timers expire on the
    // tick after the 1 MHz boundary, and a 1 MHz access lands on an odd
    // count with that tick already applied (see the stretch below).
    if (!(cpu->cyc & 1)) {
        machine_update_irq(m, cpu);
        return;
    }
    via_tick(&m->sysvia);
    via_tick(&m->usrvia);
    machine_update_irq(m, cpu);
}

// ---- small peripherals ---------------------------------------------------------

static uint8_t acia_status(const struct machine *m)
{
    int cts = (m->serial_ula & 0x40) != 0;  // RS423 selected, no device
    uint8_t st = cts ? 0x08 : 0x02;
    // Transmit interrupt (TCB = 01) fires when TDRE shows and CTS is low.
    if ((m->acia_ctrl & 0x60) == 0x20 && !cts) {
        st |= 0x80;
    }
    return st;
}

static uint8_t adc_status(const struct machine *m)
{
    uint8_t st = m->adc_ctrl & 0x0f;  // channel, input flag, 12-bit mode
    st |= 0x20;                         // MSB of the 0x8000 centre value
    if (!m->adc_busy) {
        st |= 0x40;
    }
    if (!m->adc_ready) {
        st |= 0x80;
    }
    return st;
}

static void adc_write(struct machine *m, uint8_t val)
{
    m->adc_ctrl = val;
    m->adc_busy = 1;
    m->adc_ready = 0;
    // 10 ms (12 bit) or 4 ms in 2 MHz cycles, counted from the end of the
    // stretched write as beebjit does; the +1 is the cycle that completes
    // the write after this call.
    m->adc_countdown = (val & 0x08) ? 20001 : 8001;
    via_set_cb1(&m->sysvia, 1);
}

// ---- I/O ----------------------------------------------------------------------

// beebjit's Master rule, from Tom Seddon: everything in FC00-FEFF is 1 MHz
// except FE20-FE27, FE2C-FE3F and FEA1-FEFF.
INLINE int is_1mhz(uint16_t addr)
{
    if ((addr & 0xff00) != 0xfe00) {
        return 1;
    }
    if (addr > 0xfea0) {
        return 0;
    }
    if (addr >= 0xfe20 && addr < 0xfe28) {
        return 0;
    }
    if (addr >= 0xfe2c && addr < 0xfe40) {
        return 0;
    }
    return 1;
}


static uint8_t sheila_read(struct machine *m, struct cpu6502 *cpu, uint16_t addr)
{
    uint8_t ret = 0xfe;
    switch (addr & 0xfffc) {
    case 0xfe00: case 0xfe04:
        ret = crtc_read(&m->crtc, addr & 1);
        break;
    case 0xfe08: case 0xfe0c:
        ret = (addr & 1) ? 0 : acia_status(m);
        break;
    case 0xfe10: case 0xfe14:
        // The serial ULA has no read line: a read writes the high address
        // byte, 0xFE, into it (beebjit, from a real machine).
        m->serial_ula = 0xfe;
        ret = 0;
        break;
    case 0xfe18:
        switch (addr & 3) {
        case 0: ret = adc_status(m); break;
        case 1: ret = 0x80; break;  // high byte of the 0x8000 centre value
        case 2: ret = 0x00; break;
        default: ret = 0; break;
        }
        break;
    case 0xfe1c:
        ret = 0;
        break;
    case 0xfe24:
        ret = 0;
        break;
    case 0xfe28:
        ret = 0;  // 1770 idle; Phase 4 replaces this
        break;
    case 0xfe30:
        ret = m->romsel;
        break;
    case 0xfe34:
        ret = m->acccon;
        break;
    case 0xfe40: case 0xfe44: case 0xfe48: case 0xfe4c:
    case 0xfe50: case 0xfe54: case 0xfe58: case 0xfe5c:
        ret = via_read(&m->sysvia, addr & 0xf);
        machine_update_irq(m, cpu);
        break;
    case 0xfe60: case 0xfe64: case 0xfe68: case 0xfe6c:
    case 0xfe70: case 0xfe74: case 0xfe78: case 0xfe7c:
        ret = via_read(&m->usrvia, addr & 0xf);
        machine_update_irq(m, cpu);
        break;
    default:
        break;
    }
    return ret;
}

static void sheila_write(struct machine *m, struct cpu6502 *cpu, uint16_t addr, uint8_t val)
{
    switch (addr & 0xfffc) {
    case 0xfe00: case 0xfe04:
        crtc_write(&m->crtc, addr & 1, val);
        break;
    case 0xfe08: case 0xfe0c:
        if (!(addr & 1)) {
            m->acia_ctrl = ((val & 3) == 3) ? 0 : val;  // 3 = master reset
        }
        break;
    case 0xfe10: case 0xfe14:
        m->serial_ula = val;
        break;
    case 0xfe18:
        if ((addr & 3) == 0) {
            adc_write(m, val);
        }
        break;
    case 0xfe20:
        if (addr & 1) {
            m->ula_pal[val >> 4] = val & 0xf;
        } else {
            m->ula_ctrl = val;
            crtc_set_ula(&m->crtc, val);
        }
        break;
    case 0xfe24:
        m->fdc_ctrl = val;
        break;
    case 0xfe30:
        m->romsel = val;
        remap_sideways(m);
        break;
    case 0xfe34:
        m->acccon = val;
        remap_shadow(m);
        remap_hazel(m);
        break;
    case 0xfe40: case 0xfe44: case 0xfe48: case 0xfe4c:
    case 0xfe50: case 0xfe54: case 0xfe58: case 0xfe5c:
        via_write(&m->sysvia, addr & 0xf, val);
        machine_update_irq(m, cpu);
        break;
    case 0xfe60: case 0xfe64: case 0xfe68: case 0xfe6c:
    case 0xfe70: case 0xfe74: case 0xfe78: case 0xfe7c:
        via_write(&m->usrvia, addr & 0xf, val);
        machine_update_irq(m, cpu);
        break;
    default:
        break;
    }
}

// FRED and JIM (0xFC00-0xFDFF): nothing there on this machine, reads 0xFF.
INLINE uint8_t fred_jim_read(struct machine *m, uint16_t addr)
{
    (void)m;
    (void)addr;
    return 0xff;
}

static uint8_t machine_io_rd(struct cpu6502 *cpu, uint16_t addr);
static void machine_io_wr(struct cpu6502 *cpu, uint16_t addr, uint8_t val);

// ---- the core, specialised for this machine -------------------------------

#define CPU6502_TICK(cpu) machine_tick(cpu)
#define CPU6502_IO_RD(cpu, addr) machine_io_rd(cpu, addr)
#define CPU6502_IO_WR(cpu, addr, v) machine_io_wr(cpu, addr, v)
#define CPU6502_CODE_ATTR MACHINE_CODE_ATTR
#include "cpu6502.c"

#define tick1(cpu) cycle(cpu)

static uint8_t machine_io_rd(struct cpu6502 *cpu, uint16_t addr)
{
    struct machine *m = cpu->m;
    uint8_t v;
    if ((addr & 0xff00) != 0xfe00) {
        tick1(cpu);
        if (!(cpu->cyc & 1)) {
            tick1(cpu);
        }
        v = fred_jim_read(m, addr);
        tick1(cpu);
        return v;
    }
    if (is_1mhz(addr)) {
        tick1(cpu);
        if (cpu->cyc & 1) {  // c was even: one idle cycle, now mid 1 MHz
            v = sheila_read(m, cpu, addr);
            tick1(cpu);
        } else {             // c was odd: two idle cycles
            tick1(cpu);
            v = sheila_read(m, cpu, addr);
            tick1(cpu);
        }
        return v;
    }
    tick1(cpu);
    return sheila_read(m, cpu, addr);
}

static void machine_io_wr(struct cpu6502 *cpu, uint16_t addr, uint8_t val)
{
    struct machine *m = cpu->m;
    if ((addr & 0xff00) != 0xfe00) {
        tick1(cpu);
        if (!(cpu->cyc & 1)) {
            tick1(cpu);
        }
        tick1(cpu);  // FRED, JIM: 1 MHz, nothing listening
        return;
    }
    if (is_1mhz(addr)) {
        tick1(cpu);
        if (cpu->cyc & 1) {
            sheila_write(m, cpu, addr, val);
            tick1(cpu);
        } else {
            tick1(cpu);
            sheila_write(m, cpu, addr, val);
            tick1(cpu);
        }
        return;
    }
    if (addr >= 0xfe20 && addr < 0xfe24) {
        sheila_write(m, cpu, addr, val);  // video ULA: access then tick
        tick1(cpu);
        return;
    }
    tick1(cpu);
    sheila_write(m, cpu, addr, val);
}

// ---- lifecycle --------------------------------------------------------------

// jsbeeb's CMOS defaults, via beebjit: MODE 7 boot... as a real unconfigured
// Master would look after *CONFIGURE defaults.
static const uint8_t cmos_defaults[64] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xc9, 0xff, 0xff, 0x12, 0x00,
    0x17, 0xca, 0x1e, 0x05, 0x00, 0x35, 0xa6, 0x00,
};

void machine_init(struct machine *m)
{
    memset(m, 0, sizeof *m);
    memcpy(m->cmos_ram, cmos_defaults, sizeof cmos_defaults);
    m->cpu.cmos = 1;
    m->cpu.m = m;
    m->cpu.rd_tabs[0] = m->rd_tab[0];
    m->cpu.rd_tabs[1] = m->rd_tab[1];
    m->cpu.wr_tabs[0] = m->wr_tab[0];
    m->cpu.wr_tabs[1] = m->wr_tab[1];
    m->sysvia.port_cb = sysvia_port_cb;
    m->sysvia.ctx = m;
}

void machine_reset(struct machine *m)
{
    // Power-on RAM contents as beebjit models them (and as many real BBCs
    // behave): main RAM 0xFF except zero page, the Master's extra RAM zero.
    memset(m->ram, 0xff, sizeof m->ram);
    memset(m->ram, 0x00, 0x100);
    memset(m->lynne, 0, sizeof m->lynne);
    memset(m->andy, 0, sizeof m->andy);
    memset(m->hazel, 0, sizeof m->hazel);
    memset(m->swram, 0, sizeof m->swram);
    m->romsel = 0;
    m->acccon = 0;
    m->ic32 = 0;
    remap_all(m);
    via_reset(&m->sysvia);
    via_reset(&m->usrvia);
    m->ula_ctrl = 0x02;  // teletext, 1 MHz, as beebjit's power-on ULA
    crtc_set_ula(&m->crtc, m->ula_ctrl);
    m->crtc.screen_wrap_add = 0;
    crtc_reset(&m->crtc);
    m->cpu.cycles = 0;
    m->cpu.cyc = 0;
    cpu6502_reset(&m->cpu);
}

uint64_t machine_run(struct machine *m, uint64_t until)
{
    return cpu6502_run(&m->cpu, until);
}
