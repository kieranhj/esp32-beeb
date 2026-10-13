// esp32-beeb: disc images on the microSD card. The card is mounted at boot,
// its .ssd/.dsd files listed (the root and /discs), the chosen one read into
// PSRAM, and then unmounted: FATFS and the SDMMC driver hold internal RAM
// the running machine needs more.
#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

struct sdcard_file {
    char name[32];        // the file name, as listed (long names kept)
    uint32_t size;
    uint8_t in_discs;     // in /discs rather than the root
};

// Mounts the card (1-bit SDMMC; its D3 is on the IO expander, driven high
// for SD mode) and lists the disc images. ESP_ERR_NOT_FOUND without a card.
esp_err_t sdcard_open(i2c_master_bus_handle_t i2c);
int sdcard_count(void);
const struct sdcard_file *sdcard_file(int i);
// Reads one image into PSRAM (free it with heap_caps_free); NULL on error.
uint8_t *sdcard_read(int i, uint32_t *size);
// Unmounts and frees the driver.
void sdcard_close(void);

#endif
