// Host harness: boots the Master and prints an instruction trace in the
// field order beebjit uses, for tools/tracediff.py.
//
//   trace <roms-dir> <cycles> > ours.txt
//
// Each line: "PC A X Y S F cyc" where F is beebjit's 8-character flag field
// ("CZID 1ON" with spaces for clear bits) and cyc the 2 MHz cycle count at
// the opcode fetch. Interrupts print as "IRQ" or "NMI" lines with the same
// fields so they can be matched against beebjit's "IRQ (IRQ)" entries.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPU6502_ORACLE_COMPAT 1
#define CPU6502_TRACE(cpu) trace_line(cpu, 0)
#define CPU6502_TRACE_IRQ(cpu, nmi) trace_line(cpu, (nmi) ? 2 : 1)

struct cpu6502;
static void trace_line(struct cpu6502 *cpu, int kind);

#include "../main/machine.c"

static int debug_irq;

static void trace_line(struct cpu6502 *cpu, int kind)
{
    char f[9];
    if (kind && debug_irq) {
        struct machine *mm = cpu->m;
        fprintf(stderr, "%s at %lu pc=%04X: sysvia ifr=%02X ier=%02X  usrvia ifr=%02X ier=%02X\n",
                kind == 1 ? "IRQ" : "NMI", (unsigned long)cpu->cyc, cpu->pc,
                mm->sysvia.ifr, mm->sysvia.ier, mm->usrvia.ifr, mm->usrvia.ier);
    }
    memset(f, ' ', 8);
    f[8] = 0;
    if (cpu->c) f[0] = 'C';
    if (cpu->z) f[1] = 'Z';
    if (cpu->i) f[2] = 'I';
    if (cpu->d) f[3] = 'D';
    f[5] = '1';
    if (cpu->v) f[6] = 'O';
    if (cpu->n) f[7] = 'N';
    printf("%s%04X %02X %02X %02X %02X %s %lu\n",
           kind == 0 ? "" : (kind == 1 ? "IRQ " : "NMI "),
           cpu->pc, cpu->a, cpu->x, cpu->y, cpu->s, f, (unsigned long)cpu->cyc);
}

static const uint8_t *load_rom(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    uint8_t *buf = malloc(16384);
    size_t n = fread(buf, 1, 16384, f);
    fclose(f);
    if (n != 16384) {
        fprintf(stderr, "%s: short read (%zu)\n", path, n);
        exit(2);
    }
    return buf;
}

static struct machine machine;

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: trace <roms-dir> <cycles>\n");
        return 2;
    }
    const char *dir = argv[1];
    debug_irq = getenv("TRACE_DEBUG") != NULL;
    uint64_t cycles = strtoull(argv[2], NULL, 10);

    struct machine *m = &machine;
    machine_init(m);
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) {
        m->swram[i] = calloc(0x4000, 1);
    }
    m->mos = load_rom(dir, "mos.rom");
    m->rom[0x9] = load_rom(dir, "dfs.rom");
    m->rom[0xa] = load_rom(dir, "viewsht.rom");
    m->rom[0xb] = load_rom(dir, "edit.rom");
    m->rom[0xc] = load_rom(dir, "basic4.rom");
    m->rom[0xd] = load_rom(dir, "adfs.rom");
    m->rom[0xe] = load_rom(dir, "view.rom");
    m->rom[0xf] = load_rom(dir, "terminal.rom");
    machine_reset(m);

    while (m->cpu.cycles < cycles) {
        machine_run(m, m->cpu.cycles + 100000);
    }
    fprintf(stderr, "ran %llu cycles, pc=%04x\n",
            (unsigned long long)m->cpu.cycles, m->cpu.pc);
    return 0;
}
