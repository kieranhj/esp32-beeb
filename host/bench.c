// Host benchmark: runs the Master for N cycles with no trace, prints speed
// and the machine's path counters. With --from A the counters cover only
// cycles A..N (a window inside a demo); --disc and --autoboot as frame.c.
//   zig cc -O2 -std=c11 -o host/bench.exe host/bench.c
//   host/bench.exe roms/mos3.20 10000000 --disc discs/x.ssd --autoboot --from 9000000
#define MACHINE_STATS 1
#define MACHINE_CYCLES() __builtin_readcyclecounter()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../main/machine.c"
static struct machine machine;
static const uint8_t *load_rom(const char *dir, const char *name)
{
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(2); }
    uint8_t *buf = malloc(16384); fread(buf, 1, 16384, f); fclose(f); return buf;
}
static struct disc *load_disc(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *image = malloc((size_t)size);
    fread(image, 1, (size_t)size, f);
    fclose(f);
    size_t len = strlen(path);
    int sides = (len > 4 && (strcmp(path + len - 4, ".dsd") == 0)) ? 2 : 1;
    struct disc *d = calloc(1, sizeof *d);
    uint32_t *pulses = malloc((size_t)sides * DISC_TRACKS * DISC_BYTES_PER_TRACK * sizeof(uint32_t));
    disc_init(d, pulses, sides, 0);
    disc_load_ssd(d, image, (uint32_t)size);
    free(image);
    return d;
}
static void reset_stats(struct machine *m)
{
    m->stat_calls = m->stat_chunks = 0;
    m->crtc.stat_fast = m->crtc.stat_slow = 0;
    m->crtc.stat_clk_fast = m->crtc.stat_clk_slow = m->crtc.stat_clk_plan = 0;
    m->crtc.stat_runs = 0;
    memset(m->crtc.stat_slow_why, 0, sizeof m->crtc.stat_slow_why);
    m->stat_io_ticks = m->stat_zero_calls = 0;
    m->stat_crtc_deferred = m->stat_crtc_deferred_vsync = 0;
    m->stat_via_fast = 0;
    m->stat_sync_boundary = m->stat_sync_io = m->stat_sync_write = 0;
    memset(m->stat_due_hist, 0, sizeof m->stat_due_hist);
    memset(m->stat_due_src, 0, sizeof m->stat_due_src);
    m->stat_clk_advance = m->stat_clk_crtc = m->stat_clk_via = m->stat_clk_fdc = m->stat_clk_io = 0;
    memset(m->stat_clk_sheila, 0, sizeof m->stat_clk_sheila);
    memset(m->stat_io_page, 0, sizeof m->stat_io_page);
}
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: bench <roms-dir> <cycles> [--disc f] [--autoboot] [--from A]\n"); return 2; }
    uint64_t cycles = strtoull(argv[2], NULL, 10);
    uint64_t from = 0;
    struct machine *m = &machine;
    machine_init(m);
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) m->swram[i] = calloc(0x4000, 1);
    m->mos = load_rom(argv[1], "mos.rom");
    m->rom[0x9] = load_rom(argv[1], "dfs.rom");
    m->rom[0xa] = load_rom(argv[1], "viewsht.rom");
    m->rom[0xb] = load_rom(argv[1], "edit.rom");
    m->rom[0xc] = load_rom(argv[1], "basic4.rom");
    m->rom[0xd] = load_rom(argv[1], "adfs.rom");
    m->rom[0xe] = load_rom(argv[1], "view.rom");
    m->rom[0xf] = load_rom(argv[1], "terminal.rom");
    machine_reset(m);
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--disc") == 0 && i + 1 < argc) {
            machine_set_disc(m, 0, load_disc(argv[++i]));
        } else if (strcmp(argv[i], "--autoboot") == 0) {
            machine_autoboot(m);
        } else if (strcmp(argv[i], "--from") == 0 && i + 1 < argc) {
            from = strtoull(argv[++i], NULL, 10);
        }
    }
    while (m->cpu.cycles < from) machine_run(m, m->cpu.cycles + 100000);
    reset_stats(m);
    uint64_t start = m->cpu.cycles;
    clock_t t0 = clock();
    uint64_t tsc0 = __builtin_readcyclecounter();
    while (m->cpu.cycles < cycles) machine_run(m, m->cpu.cycles + 100000);
    double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    uint64_t ran = m->cpu.cycles - start;
    printf("%llu cycles in %.2f s: %.1f M cycles/s, %.1fx realtime (pc=%04x, drive0 %s)\n",
           (unsigned long long)ran, s, ran / s / 1e6, ran / s / 2e6, m->cpu.pc,
           m->fdc.drive[0].spinning ? "spinning" : "stopped");
    printf("per 1M cycles: calls=%.0f chunks=%.0f crtc_fast=%.0f crtc_slow=%.0f\n",
           m->stat_calls * 1e6 / ran, m->stat_chunks * 1e6 / ran,
           m->crtc.stat_fast * 1e6 / ran, m->crtc.stat_slow * 1e6 / ran);
    printf("per 1M cycles: sync_boundary=%.0f io=%.0f writes=%.0f\n",
           m->stat_sync_boundary * 1e6 / ran, m->stat_sync_io * 1e6 / ran, m->stat_sync_write * 1e6 / ran);
    {
        uint64_t total = __builtin_readcyclecounter() - tsc0;
        printf("host clocks per emulated cycle: total %.1f  advance %.1f (crtc %.1f via %.1f fdc %.1f)  io slow %.1f\n",
               (double)total / ran, (double)m->stat_clk_advance / ran, (double)m->stat_clk_crtc / ran,
               (double)m->stat_clk_via / ran, (double)m->stat_clk_fdc / ran, (double)m->stat_clk_io / ran);
    }
    printf("crtc: plan %.1f clk/cyc, fast %.1f clk/tick, slow %.1f clk/tick\n", (double)m->crtc.stat_clk_plan / ran,
           m->crtc.stat_fast ? (double)m->crtc.stat_clk_fast / m->crtc.stat_fast : 0.0,
           m->crtc.stat_slow ? (double)m->crtc.stat_clk_slow / m->crtc.stat_slow : 0.0);
    printf("via accesses through the quiet window: %u (%.0f per 1M)\n", m->stat_via_fast, m->stat_via_fast * 1e6 / ran);
    printf("crtc left behind by %u calls (%.0f per 1M), vsync changes found catching up: %u (must be 0)\n",
           m->stat_crtc_deferred, m->stat_crtc_deferred * 1e6 / ran, m->stat_crtc_deferred_vsync);
    printf("per 1M cycles: fast runs %.0f (avg %.1f ticks), io ticks %.0f, zero-length calls %.0f\n",
           m->crtc.stat_runs * 1e6 / ran, m->crtc.stat_runs ? (double)m->crtc.stat_fast / m->crtc.stat_runs : 0.0,
           m->stat_io_ticks * 1e6 / ran, m->stat_zero_calls * 1e6 / ran);
    printf("exact ticks per 1M by cause: line-start %.0f hsync %.0f cursor-skew %.0f dispen-pipe %.0f counter-hit %.0f other %.0f\n",
           m->crtc.stat_slow_why[0] * 1e6 / ran, m->crtc.stat_slow_why[1] * 1e6 / ran, m->crtc.stat_slow_why[2] * 1e6 / ran,
           m->crtc.stat_slow_why[3] * 1e6 / ran, m->crtc.stat_slow_why[4] * 1e6 / ran, m->crtc.stat_slow_why[5] * 1e6 / ran);
    printf("sheila by page (FE0x crtc/acia, FE1x serial/adc, FE2x ula/fdc, FE3x paging, FE4x-5x sysvia, FE6x-7x usrvia): ops per 1M, host clocks per op\n");
    for (int i = 0; i < 8; i++) {
        if (m->stat_io_page[i]) {
            printf("  FE%X0: %.0f ops, %.0f clk/op\n", i, m->stat_io_page[i] * 1e6 / ran,
                   (double)m->stat_clk_sheila[i] / m->stat_io_page[i]);
        }
    }
    printf("due source (cap via adc key crtc fdc nmi):");
    for (int i = 0; i < 7; i++) printf(" %u", m->stat_due_src[i]);
    putchar(10);
    printf("due_hist (log2 cycles to next due):");
    for (int i = 0; i < 17; i++) printf(" %u", m->stat_due_hist[i]);
    putchar(10);
    return 0;
}
