// Host harness for the sound path: runs the machine, feeds the SN76489 the
// bytes the system VIA lands on it, and writes the chip's output as a
// 31250 Hz mono WAV (eight chip steps averaged per sample, as the board).
//   zig cc -O2 -std=c11 -o host/sound.exe host/sound.c
//   host/sound.exe roms/mos3.20 4000000 out/boot.wav            # the boot beep
//   host/sound.exe roms/mos3.20 60000000 out/x.wav --disc discs/x.ssd --autoboot
// Prints the byte count, the peak and the loudest 20 ms window's frequency
// from zero crossings, so a result can be checked without listening.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../main/sn76489.h"
#include "../main/sn76489.c"

static struct sn76489 sn;
static int16_t *wav;
static uint32_t wav_n, wav_cap;
static uint32_t rendered, phase, nsteps;
static int16_t steps[8];
static uint32_t writes;

static void render_to(uint32_t cycle)
{
    uint32_t n = cycle - rendered;
    uint32_t cycles = phase + n;
    uint32_t whole = cycles / 8;
    phase = cycles % 8;
    rendered = cycle;
    while (whole) {
        uint32_t k = 8 - nsteps;
        if (k > whole) k = whole;
        sn_render(&sn, steps + nsteps, k);
        nsteps += k;
        whole -= k;
        if (nsteps == 8) {
            int32_t sum = 0;
            for (int i = 0; i < 8; i++) sum += steps[i];
            if (wav_n == wav_cap) {
                wav_cap = wav_cap ? wav_cap * 2 : 1 << 20;
                wav = realloc(wav, wav_cap * sizeof *wav);
            }
            {
                static int32_t dc_x, dc_y;   // the board's DC blocker
                int32_t x = sum / 8;
                int32_t y = x - dc_x + (dc_y * 1015) / 1024;
                dc_x = x;
                dc_y = y;
                wav[wav_n++] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
            }
            nsteps = 0;
        }
    }
}

static void sound_byte(uint32_t cycle, uint8_t byte)
{
    render_to(cycle);
    sn_write(&sn, byte);
    writes++;
    if (getenv("SOUND_DEBUG")) {
        fprintf(stderr, "sn %u: %02x  periods %u %u %u %u volumes %u %u %u %u\n", cycle, byte,
                sn.period[0], sn.period[1], sn.period[2], sn.period[3], sn.volume[0], sn.volume[1], sn.volume[2], sn.volume[3]);
    }
}

#define MACHINE_SOUND_WRITE(m, byte) sound_byte((m)->synced, (byte))
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
static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: sound <roms-dir> <cycles> <out.wav> [--disc f] [--autoboot]\n"); return 2; }
    uint64_t cycles = strtoull(argv[2], NULL, 10);
    struct machine *m = &machine;
    sn_reset(&sn);
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
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--disc") == 0 && i + 1 < argc) {
            machine_set_disc(m, 0, load_disc(argv[++i]));
        } else if (strcmp(argv[i], "--autoboot") == 0) {
            machine_autoboot(m);
        }
    }
    while (m->cpu.cycles < cycles) machine_run(m, m->cpu.cycles + 100000);
    render_to((uint32_t)m->cpu.cycles);

    FILE *f = fopen(argv[3], "wb");
    if (!f) { perror(argv[3]); return 2; }
    uint32_t bytes = wav_n * 2;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, 31250); put32(f, 31250 * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, bytes);
    fwrite(wav, 2, wav_n, f);
    fclose(f);

    // Peak, and the loudest 20 ms window's zero-crossing rate.
    int peak = 0;
    uint32_t win = 625, best = 0;
    long long best_e = -1;
    for (uint32_t i = 0; i + win <= wav_n; i += win / 2) {
        long long e = 0;
        for (uint32_t j = 0; j < win; j++) {
            int v = wav[i + j];
            e += (long long)v * v;
            if (v > peak) peak = v;
            if (-v > peak) peak = -v;
        }
        if (e > best_e) { best_e = e; best = i; }
    }
    int crossings = 0;
    for (uint32_t j = best + 1; j < best + win; j++) {
        if ((wav[j - 1] < 0) != (wav[j] < 0)) crossings++;
    }
    printf("%u bytes to the chip, %u samples (%.2f s), peak %d, loudest 20 ms at %.2f s: about %d Hz\n",
           writes, wav_n, wav_n / 31250.0, peak, best / 31250.0, crossings * 31250 / (int)win / 2);
    // Per half second: RMS and the zero-crossing rate as a frequency.
    for (uint32_t i = 0; i + 15625 <= wav_n; i += 15625) {
        double e = 0;
        int zc = 0;
        for (uint32_t j = i; j < i + 15625; j++) {
            e += (double)wav[j] * wav[j];
            if (j > i && (wav[j - 1] < 0) != (wav[j] < 0)) zc++;
        }
        printf("  %4.1f s: rms %6.0f  ~%5d Hz\n", i / 31250.0, __builtin_sqrt(e / 15625), zc);
    }
    return 0;
}
