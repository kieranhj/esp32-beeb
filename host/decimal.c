// Host harness: Klaus Dormann's decimal test (d0.bin, cputype=0 = NMOS).
// Program is loaded at 0x200, runs to the STP at 0x24e; ERROR is zp 0x0b.
#include <stdio.h>
#include <string.h>
#include "../main/cpu6502.h"

static uint8_t mem[65536];

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: decimal d0.bin\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 2; }
    size_t n = fread(mem + 0x200, 1, 0x10000 - 0x200, f);
    fclose(f);
    printf("loaded %zu bytes at 0200\n", n);

    struct cpu6502 cpu;
    memset(&cpu, 0, sizeof cpu);
    cpu.mem = mem;
    mem[0xfffc] = 0x00; mem[0xfffd] = 0x02;
    mem[0x0b] = 0xff;
    cpu6502_reset(&cpu);
    while (cpu.pc != 0x24e && cpu.cycles < 200000000ULL) {
        cpu6502_run(&cpu, cpu.cycles + 1);
    }
    printf("pc=%04x cycles=%llu ERROR=%u N1=%02x N2=%02x\n", cpu.pc,
           (unsigned long long)cpu.cycles, mem[0x0b], mem[0], mem[1]);
    if (cpu.pc == 0x24e && mem[0x0b] == 0) { printf("PASS\n"); return 0; }
    printf("FAIL\n");
    return 1;
}
