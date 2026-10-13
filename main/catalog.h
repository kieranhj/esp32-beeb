// esp32-beeb: every disc the board can boot, from the flash bundle and the
// microSD card, as one list. A card image with a bundle disc's name takes
// its place (an updated disc on the card wins).
#ifndef CATALOG_H
#define CATALOG_H

#include <stdint.h>

struct catalog_entry {
    char name[32];
    uint32_t size;
    uint8_t sides;        // 1 (.ssd) or 2 (.dsd)
    uint8_t on_sd;        // else in the bundle
    int16_t index;        // in the bundle or in the card's list
};

// Builds the list; call after bundle_open() and sdcard_open().
void catalog_build(void);
int catalog_count(void);
const struct catalog_entry *catalog_entry(int i);
int catalog_find(const char *name);                       // index, or -1
// The image in PSRAM; NULL if it cannot be read. Release it after use.
const uint8_t *catalog_load(int i);
void catalog_release(void);

#endif
