// Host benchmark: runs the Master for N cycles with no trace, prints speed.
#define MACHINE_STATS 1
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../main/machine.c"
static struct machine machine;
static const uint8_t *load_rom(const char *dir, const char *name)
{
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(2); }
    uint8_t *buf = malloc(16384); fread(buf, 1, 16384, f); fclose(f); return buf;
}
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: bench <roms-dir> <cycles>\n"); return 2; }
    uint64_t cycles = strtoull(argv[2], NULL, 10);
    struct machine *m = &machine;
    machine_init(m);
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) m->swram[i] = calloc(0x4000, 1);
    m->mos = load_rom(argv[1], "mos.rom");
    m->rom[0x9] = load_rom(argv[1], "dfs.rom");
    m->rom[0xc] = load_rom(argv[1], "basic4.rom");
    machine_reset(m);
    clock_t t0 = clock();
    while (m->cpu.cycles < cycles) machine_run(m, m->cpu.cycles + 100000);
    double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("%llu cycles in %.2f s: %.1f M cycles/s, %.1fx realtime\n",
           (unsigned long long)m->cpu.cycles, s, m->cpu.cycles / s / 1e6, m->cpu.cycles / s / 2e6);
    printf("STATS calls=%u chunks=%u crtc_fast=%u crtc_slow=%u\n",
           m->stat_calls, m->stat_chunks, m->crtc.stat_fast, m->crtc.stat_slow);
    printf("STATS sync_boundary=%u io_rd=%u writes=%u\n",
           m->stat_sync_boundary, m->stat_sync_io, m->stat_sync_write);
    printf("STATS writes=%u due_hist:", m->stat_sync_write);
    for (int i = 0; i < 17; i++) printf(" %u", m->stat_due_hist[i]);
    putchar(10);
    return 0;
}
