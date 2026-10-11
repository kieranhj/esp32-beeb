// Host harness: runs Klaus Dormann's 6502 functional test image on the core.
// Usage: klaus <6502.bin>
// Passes when PC settles at 0x3448 (the success trap in that build).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../main/cpu6502.h"

static uint8_t mem[65536];

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: klaus 6502.bin\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(mem, 1, 65536, f) != 65536) { perror("read"); return 2; }
    fclose(f);

    struct cpu6502 cpu;
    memset(&cpu, 0, sizeof cpu);
    cpu.mem = mem;
    mem[0xfffc] = 0x00; mem[0xfffd] = 0x04;
    cpu6502_reset(&cpu);

    clock_t t0 = clock();
    uint16_t last = 0xffff;
    for (;;) {
        uint16_t pc = cpu.pc;
        cpu6502_run(&cpu, cpu.cycles + 1);
        if (cpu.pc == last && pc == last) break;  // jmp * trap
        last = pc;
        if (cpu.cycles > 400000000ULL) { printf("timeout\n"); return 1; }
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("stopped at pc=%04x after %llu cycles (%.2f s, %.1f M cycles/s)\n",
           cpu.pc, (unsigned long long)cpu.cycles, secs, cpu.cycles / secs / 1e6);
    if (cpu.pc == 0x3448) { printf("PASS\n"); return 0; }
    printf("FAIL\n");
    return 1;
}
