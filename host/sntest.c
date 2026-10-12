// Checks sn_render8 against eight sn_render steps summed, over random
// register writes (tones, noise of both types, periods 0 to 0x3ff).
//   zig cc -O2 -std=c11 -o host/sntest.exe host/sntest.c && host/sntest.exe
#include <stdio.h>
#include <stdlib.h>
#include "../main/sn76489.h"
#include "../main/sn76489.c"

int main(void)
{
    static struct sn76489 a, b;
    sn_reset(&a);
    sn_reset(&b);
    srand(1);
    long long samples = 0, bad = 0;
    for (int round = 0; round < 200000; round++) {
        if (rand() % 4 == 0) {
            uint8_t v = (uint8_t)rand();
            if (rand() % 8 == 0) {
                v = (uint8_t)(0xe0 | (rand() & 7));   // noise control
            }
            sn_write(&a, v);
            sn_write(&b, v);
        }
        int n = 1 + rand() % 40;
        for (int i = 0; i < n; i++) {
            int16_t steps[8];
            sn_render(&a, steps, 8);
            int32_t want = 0;
            for (int k = 0; k < 8; k++) {
                want += steps[k];
            }
            int32_t got = sn_render8(&b);
            samples++;
            if (got != want) {
                if (bad < 5) {
                    printf("mismatch at sample %lld: want %d got %d\n", samples, want, got);
                }
                bad++;
            }
        }
    }
    printf("%lld samples, %lld mismatches\n", samples, bad);
    return bad != 0;
}
