// esp32-beeb: boots the Master on core 1 and, for now, measures it.
//
// Until Phase 3 there is no display, so this runs MOS 3.20 for a fixed
// number of emulated cycles, reports host clocks per emulated cycle with the
// whole machine (CPU, VIAs, CRTC, ADC) ticking, and prints the mode 7 screen
// as text so the boot can be seen to have reached the BASIC prompt.

#define MACHINE_STATS 1
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "machine.h"

static const char *TAG = "beeb";

// The ROM set, read in place from flash through the data cache.
#define ROM(sym) \
    extern const uint8_t sym##_start[] asm("_binary_" #sym "_rom_start");
ROM(mos) ROM(dfs) ROM(viewsht) ROM(edit) ROM(basic4) ROM(adfs) ROM(view) ROM(terminal)

// Everything the emulation touches per cycle lives here, in internal SRAM.
static DRAM_ATTR struct machine machine;
// Experiment: MOS in internal RAM instead of flash-through-dcache.
static DRAM_ATTR uint8_t mos_ram[16384];

static void print_screen(struct machine *m)
{
    // MODE 7: 25 rows of 40 characters from 0x7C00 in main RAM.
    for (int row = 0; row < 25; row++) {
        char line[41];
        for (int col = 0; col < 40; col++) {
            uint8_t c = m->ram[0x7c00 + row * 40 + col] & 0x7f;
            line[col] = (c >= 32 && c < 127) ? (char)c : ' ';
        }
        line[40] = 0;
        printf("|%s|\n", line);
    }
}

static void beeb_task(void *arg)
{
    (void)arg;
    struct machine *m = &machine;

    machine_init(m);
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) {
        // PSRAM for now: a demo running code from sideways RAM will pay for
        // it; see decisions.md. Internal SRAM is the plan once Phase 3 has
        // sized the display buffers.
        m->swram[i] = heap_caps_malloc(0x4000, MALLOC_CAP_SPIRAM);
    }
    memcpy(mos_ram, mos_start, sizeof mos_ram);
    m->mos = mos_ram;
    m->rom[0x9] = dfs_start;
    m->rom[0xa] = viewsht_start;
    m->rom[0xb] = edit_start;
    m->rom[0xc] = basic4_start;
    m->rom[0xd] = adfs_start;
    m->rom[0xe] = view_start;
    m->rom[0xf] = terminal_start;
    machine_reset(m);

    printf("\nBEEB begin on core %d, machine struct %u bytes\n",
           xPortGetCoreID(), (unsigned)sizeof machine);

    // Four runs of 10 s emulated time each: full machine, then without
    // the CRTC step, without the VIA step, and with neither (the cost of the
    // per-cycle call itself).
    static const uint8_t masks[4] = {0, 1, 3, 7};
    for (int run = 0; run < 4; run++) {
        machine_profile_skip = masks[run];
        const uint64_t slice = 20000000;
        uint64_t target = m->cpu.cycles + slice;
        int64_t t0 = esp_timer_get_time();
        while (m->cpu.cycles < target) {
            machine_run(m, m->cpu.cycles + 100000);
        }
        int64_t us = esp_timer_get_time() - t0;
        double mcps = (double)slice / (double)us;
        printf("RESULT machine skip%d cycles=%llu us=%lld  %6.2f Mcyc/s  %6.1f clk/cyc  %5.2fx realtime  frames=%lu pc=%04X\n",
               masks[run], (unsigned long long)slice, (long long)us, mcps,
               240.0 * (double)us / (double)slice, mcps / 2.0,
               (unsigned long)m->crtc.frames, m->cpu.pc);
        printf("STATS calls=%lu chunks=%lu crtc_fast=%lu crtc_slow=%lu\n",
               (unsigned long)m->stat_calls, (unsigned long)m->stat_chunks,
               (unsigned long)m->crtc.stat_fast, (unsigned long)m->crtc.stat_slow);
        m->stat_calls = m->stat_chunks = 0;
        m->crtc.stat_fast = m->crtc.stat_slow = 0;
    }
    print_screen(m);
    printf("BEEB end\n");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "esp32-beeb");
    ESP_LOGI(TAG, "internal heap free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    xTaskCreatePinnedToCore(beeb_task, "beeb", 16384, NULL, 5, NULL, 1);
}
