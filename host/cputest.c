// Host harness for the Klaus Dormann test images.
//
//   cputest <image> <load-addr-hex> <start-pc-hex> <nmos|cmos> <stop>
//
// <stop> is a hex PC to stop at, or "trap" to stop when the PC stops
// changing (the tests' "jmp *" success and failure traps). Prints the final
// PC, cycle count and zero page byte 0x0B (the decimal test's ERROR flag).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../main/cpu6502.h"

static uint8_t mem[65536];
static uint8_t *table[256];

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: cputest image load start nmos|cmos stop|trap\n");
        return 2;
    }
    unsigned load = strtoul(argv[2], NULL, 16);
    unsigned start = strtoul(argv[3], NULL, 16);
    int cmos = strcmp(argv[4], "cmos") == 0;
    int trap = strcmp(argv[5], "trap") == 0;
    unsigned stop = trap ? 0 : strtoul(argv[5], NULL, 16);

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    size_t n = fread(mem + load, 1, 0x10000 - load, f);
    fclose(f);

    struct cpu6502 cpu;
    memset(&cpu, 0, sizeof cpu);
    cpu6502_map_flat(&cpu, mem, table);
    cpu.cmos = cmos;
    mem[0xfffc] = start & 0xff;
    mem[0xfffd] = start >> 8;
    cpu6502_reset(&cpu);

    clock_t t0 = clock();
    uint16_t last = 0xffff;
    for (;;) {
        uint16_t pc = cpu.pc;
        cpu6502_run(&cpu, cpu.cycles + 1);
        if (trap ? (cpu.pc == last && pc == last) : (cpu.pc == stop)) break;
        last = pc;
        if (cpu.cycles > 400000000ULL) { printf("timeout\n"); break; }
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("%s %s: loaded %zu bytes at %04x, stopped at pc=%04x after %llu cycles "
           "(%.2f s) zp0b=%02x\n", argv[1], argv[4], n, load, cpu.pc,
           (unsigned long long)cpu.cycles, secs, mem[0x0b]);
    printf("zp00:");
    for (int i = 0; i < 16; i++) printf(" %02x", mem[i]);
    putchar(10);
    return 0;
}
