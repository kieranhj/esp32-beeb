// esp32-beeb: 6522 VIA, stepped once per 1 MHz tick by the machine.
//
// Plain C, no ESP headers. The model follows beebjit's via.c, restated for a
// per-tick stepper instead of beebjit's timer events:
//   - T1 and T2 count down once per 1 MHz tick; an interrupt fires when the
//     counter passes from 0 to 0xFFFF. T1 in continuous mode reloads from the
//     latch on the tick after that, so its period is latch + 2. T1 one-shot
//     and T2 keep counting after firing and do not fire again until the high
//     counter byte is written.
//   - A write to T1CH or T2CH loads latch + 1 and the next tick brings it to
//     the latch value, which is what beebjit reads back (the write happens
//     mid 1 MHz cycle, the first decrement at the boundary that ends it).
//   - PB7 toggles on every T1 expiry whether or not ACR bit 7 is on.
//   - Port A and B pin levels come from the owning machine through the
//     peripheral_a / peripheral_b fields (1 = pulled high / not driven).
//   - CA1/CA2/CB1/CB2 are edge inputs; the machine calls via_set_c*().
// The machine owns the port side effects (keyboard, sound, CMOS, IC32)
// through via->port_cb, called after any change to ORA/ORB/DDRA/DDRB.

#ifndef VIA_H
#define VIA_H

#include <stdint.h>

enum {
    VIA_ORB = 0, VIA_ORA, VIA_DDRB, VIA_DDRA, VIA_T1CL, VIA_T1CH, VIA_T1LL,
    VIA_T1LH, VIA_T2CL, VIA_T2CH, VIA_SR, VIA_ACR, VIA_PCR, VIA_IFR, VIA_IER,
    VIA_ORANH,
};

enum {
    VIA_INT_CA2 = 0x01, VIA_INT_CA1 = 0x02, VIA_INT_SR = 0x04,
    VIA_INT_CB2 = 0x08, VIA_INT_CB1 = 0x10, VIA_INT_T2 = 0x20,
    VIA_INT_T1 = 0x40,
};

struct via {
    uint8_t ora, orb, ddra, ddrb;
    uint8_t ira, irb;         // latched inputs (ACR bits 0/1)
    uint8_t sr, acr, pcr, ifr, ier;
    uint16_t t1c, t1l, t2c, t2l;
    uint8_t t1_armed, t2_armed, t1_reload, pb7;
    uint8_t ca1, ca2, cb1, cb2;
    uint8_t peripheral_a, peripheral_b;  // pin levels from the outside
    uint8_t bus_a;            // resolved port A bus value
    uint8_t irq;              // IFR bit 7, the IRQ output
    // Port change notification: the machine recomputes peripheral levels.
    void (*port_cb)(struct via *via, void *ctx, int which);  // 0 = A, 1 = B
    void *ctx;
};

void via_reset(struct via *via);
void via_tick(struct via *via);  // one 1 MHz tick
uint8_t via_read(struct via *via, uint8_t reg);
void via_write(struct via *via, uint8_t reg, uint8_t val);
void via_set_ca1(struct via *via, int level);
void via_set_ca2(struct via *via, int level);
void via_set_cb1(struct via *via, int level);
void via_set_cb2(struct via *via, int level);
// Resolved port B pins: outputs where DDRB says so, peripheral levels else.
uint8_t via_port_b(const struct via *via);
// Resolved port A pins before the peripheral mask.
uint8_t via_port_a_out(const struct via *via);
// Recomputes bus_a from ORA/DDRA and peripheral_a; the machine calls this
// after changing peripheral_a.
void via_update_bus_a(struct via *via);

#endif
