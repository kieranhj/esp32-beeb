// esp32-beeb: the disc bundle in the `discs` flash partition, built by
// tools/make_disc_bundle.py and read in place through the flash cache.
#ifndef BUNDLE_H
#define BUNDLE_H

#include <stdint.h>
#include "esp_err.h"

struct bundle_entry {
    char name[32];        // NUL padded
    uint32_t offset;      // from the start of the partition
    uint32_t size;
    uint8_t sides;        // 1 (.ssd) or 2 (.dsd)
    uint8_t pad[3];
};

// Finds the partition and reads its directory; ESP_ERR_NOT_FOUND if there
// is no bundle.
esp_err_t bundle_open(void);
int bundle_count(void);
const struct bundle_entry *bundle_entry(int i);
int bundle_find(const char *name);                       // index, or -1
// Reads one image into PSRAM; NULL if it cannot. Release it after use.
const uint8_t *bundle_map(const struct bundle_entry *e);
void bundle_unmap(void);

#endif
