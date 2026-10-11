// esp32-beeb: the BBC Master 128 as a machine around the 65C12 core.
//
// Plain C, no ESP headers. machine.c includes via.c and cpu6502.c into its
// own translation unit so the per-cycle hook and the I/O hooks inline into
// the core; the ESP wrapper and the host harness both just include machine.c
// after defining placement attributes.
//
// Memory (all RAM in internal SRAM on the board):
//   ram[32K]      0x0000-0x7FFF main RAM
//   lynne[20K]    0x3000-0x7FFF shadow RAM, selected by ACCCON X/E rules
//   andy[4K]      0x8000-0x8FFF when ROMSEL bit 7
//   hazel[8K]     0xC000-0xDFFF when ACCCON bit 3
//   swram[4]      16 KB sideways RAM for slots 4 to 7; the platform supplies
//                 the buffers (PSRAM on the board for now, see decisions)
//   rom[16]       16 KB images for the sideways slots (NULL = empty, reads 0)
//   mos           16 KB MOS 3.20
// Paging is pointer-table updates only (machine_remap_*), never copies.

#ifndef MACHINE_H
#define MACHINE_H

#include <stdint.h>
#include "cpu6502.h"
#include "via.h"
#include "crtc.h"
#include "vstream.h"

#define MACHINE_SWRAM_SLOTS 4   // slots 4..7

struct machine {
    struct cpu6502 cpu;
    uint32_t synced;   // cycle count the peripherals have been stepped to
    uint32_t sync_due; // next cycle the core must catch up at (see machine.c)
    uint16_t wlo, whi; // display addresses fetched before sync_due
    // Tuning statistics, counted only when MACHINE_STATS is defined.
    uint32_t stat_calls, stat_chunks;
    uint32_t stat_sync_boundary, stat_sync_write, stat_sync_io;
    uint32_t stat_due_hist[17];

    uint8_t ram[0x8000];
    uint8_t lynne[0x5000];
    uint8_t andy[0x1000];
    uint8_t hazel[0x2000];
    uint8_t *swram[MACHINE_SWRAM_SLOTS];   // 16 KB each, platform allocated
    const uint8_t *rom[16];
    const uint8_t *mos;
    uint8_t bitbucket[256];     // write target for ROM pages
    uint8_t empty_rom[256];     // read source for empty slots (zeros)

    // Page tables: [0] user code, [1] code fetched from 0xC000-0xDFFF.
    uint8_t *rd_tab[2][256];
    uint8_t *wr_tab[2][256];
    // Display memory for addresses 0x3000 and up: main RAM or LYNNE, indexed
    // by the full address (the pointer is pre-offset). Set with ACCCON.
    uint8_t *display_base;

    uint8_t romsel, acccon, ic32;

    struct via sysvia, usrvia;

    // Keyboard matrix: bit (row) set in keys[col] when the key is down.
    uint8_t keys[16];

    // CMOS/RTC (HD146818) as driven through the system VIA.
    uint8_t cmos_ram[64];
    uint8_t cmos_addr;
    uint8_t cmos_enabled, cmos_strobe, cmos_data, cmos_read;

    // Serial: ACIA control and the serial ULA control byte. The ACIA status
    // is derived: TDRE unless CTS, and CTS is high whenever RS423 is
    // selected (nothing is on the other end). No tape, so DCD stays low.
    uint8_t acia_ctrl, serial_ula;

    // ADC (uPD7002): a write to the control register starts a conversion of
    // 4 ms (8 bit) or 10 ms (12 bit); completion drops system VIA CB1.
    uint8_t adc_ctrl, adc_busy, adc_ready;
    uint32_t adc_countdown;   // 2 MHz cycles

    struct crtc crtc;
    uint8_t ula_ctrl, ula_pal[16];
    struct vstream *vs;        // video stream to the renderer, or NULL

    // Stubs that Phase 4 replaces with real models.
    uint8_t fdc_ctrl;
};

extern uint8_t machine_profile_skip;   // measurement only, see machine.c
void machine_init(struct machine *m);  // zero state, build tables, VIAs
void machine_reset(struct machine *m); // power-on reset of CPU and VIAs
// Runs until the machine's total cycle count reaches `until`.
uint64_t machine_run(struct machine *m, uint64_t until);

#endif
