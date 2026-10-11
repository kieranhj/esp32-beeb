// esp32-beeb Phase 0: measure host clocks per emulated 6502 cycle.
//
// Runs two workloads (a synthetic demo-style inner loop and Klaus Dormann's
// functional test image) through three builds of the same core (IRAM, IRAM
// with a per-cycle video step, flash) and prints clocks per cycle for each.
// See PLAN.md Phase 0 and docs/decisions.md "Phase 0 results".

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "phase0.h"

static const char *TAG = "phase0";

// Internal SRAM: .bss is DRAM anyway, but be explicit, this is the point.
DRAM_ATTR uint8_t phase0_ram[65536];
DRAM_ATTR uint8_t *phase0_table[256];
DRAM_ATTR uint32_t phase0_linebuf[256];
DRAM_ATTR uint32_t phase0_expand[16];

// Klaus functional test image, embedded whole (64 KB) from the b2 tree copy.
extern const uint8_t klaus_start[] asm("_binary_6502_bin_start");

// Synthetic workload at 0x1000: a copy-and-munge loop of the kind demo inner
// loops are made of (indirect indexed load/store, abs,Y store, abs,X load,
// zp arithmetic, RMW on zp and abs, INY/BNE). 49 cycles, 12 instructions per
// inner iteration.
static const uint8_t synth[] = {
    0xa0, 0x00,        // 1000 LDY #0
    0xb1, 0x70,        // 1002 LDA (70),Y
    0x91, 0x72,        // 1004 STA (72),Y
    0x49, 0xff,        // 1006 EOR #FF
    0x99, 0x00, 0x40,  // 1008 STA 4000,Y
    0xbd, 0x00, 0x50,  // 100B LDA 5000,X
    0x65, 0x74,        // 100E ADC 74
    0x85, 0x74,        // 1010 STA 74
    0xe6, 0x75,        // 1012 INC 75
    0x46, 0x76,        // 1014 LSR 76
    0x2e, 0x00, 0x60,  // 1016 ROL 6000
    0xc8,              // 1019 INY
    0xd0, 0xe6,        // 101A BNE 1002
    0xe6, 0x71,        // 101C INC 71
    0xe6, 0x73,        // 101E INC 73
    0xe8,              // 1020 INX
    0xa5, 0x71,        // 1021 LDA 71
    0xc9, 0x70,        // 1023 CMP #70
    0xd0, 0xd9,        // 1025 BNE 1000
    0xa9, 0x30,        // 1027 LDA #30
    0x85, 0x71,        // 1029 STA 71
    0xa9, 0x38,        // 102B LDA #38
    0x85, 0x73,        // 102D STA 73
    0x4c, 0x00, 0x10,  // 102F JMP 1000
};

typedef uint64_t (*run_fn)(struct cpu6502 *, uint64_t);
typedef void (*reset_fn)(struct cpu6502 *);

struct variant {
    const char *name;
    reset_fn reset;
    run_fn run;
};

static const struct variant variants[] = {
    {"iram ", cpu6502_reset_iram, cpu6502_run_iram},
    {"video", cpu6502_reset_video, cpu6502_run_video},
    {"flash", cpu6502_reset_flash, cpu6502_run_flash},
    {"flshv", cpu6502_reset_flashv, cpu6502_run_flashv},
    {"irmft", cpu6502_reset_iramft, cpu6502_run_iramft},
};

static void load_synth(void)
{
    memset(phase0_ram, 0, sizeof phase0_ram);
    memcpy(phase0_ram + 0x1000, synth, sizeof synth);
    phase0_ram[0x70] = 0x00; phase0_ram[0x71] = 0x30;
    phase0_ram[0x72] = 0x00; phase0_ram[0x73] = 0x38;
    phase0_ram[0xfffc] = 0x00; phase0_ram[0xfffd] = 0x10;
}

static void load_klaus(void)
{
    memcpy(phase0_ram, klaus_start, 65536);
    phase0_ram[0xfffc] = 0x00; phase0_ram[0xfffd] = 0x04;
}

// Runs `cycles` emulated cycles in slices and returns host microseconds.
static int64_t timed_run(const struct variant *v, struct cpu6502 *cpu, uint64_t cycles)
{
    uint64_t target = cpu->cycles + cycles;
    int64_t t0 = esp_timer_get_time();
    while (cpu->cycles < target) {
        uint64_t slice = target - cpu->cycles;
        if (slice > 1000000) {
            slice = 1000000;
        }
        v->run(cpu, cpu->cycles + slice);
    }
    return esp_timer_get_time() - t0;
}

static void report(const char *variant, const char *work, uint64_t cycles, int64_t us)
{
    double mcps = (double)cycles / (double)us;           // M cycles per second
    double clocks = 240.0 * (double)us / (double)cycles; // 240 MHz host
    printf("RESULT %s %-6s cycles=%10llu us=%9lld  %6.2f Mcyc/s  %6.1f clk/cyc  %5.2fx realtime\n",
           variant, work, (unsigned long long)cycles, (long long)us, mcps, clocks, mcps / 2.0);
}

static void bench_task(void *arg)
{
    (void)arg;
    static struct cpu6502 cpu;
    const uint64_t synth_cycles = 20000000;  // 10 s of emulated time
    const uint64_t klaus_cycles = 20000000;

    for (int i = 0; i < 16; i++) {
        phase0_expand[i] = (uint32_t)i * 0x01010101u;
    }

    printf("\nPHASE0 begin on core %d\n", xPortGetCoreID());
    for (size_t vi = 0; vi < sizeof variants / sizeof variants[0]; vi++) {
        const struct variant *v = &variants[vi];

        load_synth();
        memset(&cpu, 0, sizeof cpu);
        cpu6502_map_flat(&cpu, phase0_ram, phase0_table);
        cpu.cmos = 1;
        v->reset(&cpu);
        int64_t us = timed_run(v, &cpu, synth_cycles);
        report(v->name, "synth", synth_cycles, us);

        load_klaus();
        memset(&cpu, 0, sizeof cpu);
        cpu6502_map_flat(&cpu, phase0_ram, phase0_table);
        cpu.cmos = 1;
        v->reset(&cpu);
        us = timed_run(v, &cpu, klaus_cycles);
        report(v->name, "klaus", klaus_cycles, us);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Correctness on target: full Klaus run through the IRAM build.
    load_klaus();
    memset(&cpu, 0, sizeof cpu);
    cpu6502_map_flat(&cpu, phase0_ram, phase0_table);
    cpu.cmos = 1;
    cpu6502_reset_iram(&cpu);
    int64_t t0 = esp_timer_get_time();
    uint16_t last = 0xffff;
    for (;;) {
        uint16_t pc = cpu.pc;
        cpu6502_run_iram(&cpu, cpu.cycles + 1);
        if (cpu.pc == last && pc == last) {
            break;
        }
        last = pc;
        if (cpu.cycles > 400000000ULL) {
            break;
        }
    }
    int64_t us = esp_timer_get_time() - t0;
    printf("KLAUS pc=%04x cycles=%llu %s (%lld us, single-stepped)\n", cpu.pc,
           (unsigned long long)cpu.cycles, cpu.pc == 0x3448 ? "PASS" : "FAIL", (long long)us);
    printf("PHASE0 end\n");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "esp32-beeb phase 0 benchmark");
    ESP_LOGI(TAG, "internal heap free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    xTaskCreatePinnedToCore(bench_task, "bench", 16384, NULL, 5, NULL, 1);
}
