// esp32-beeb: the disc bundle partition (see bundle.h and
// tools/make_disc_bundle.py).
//
// Read with esp_partition_read, not mapped. esp_partition_mmap on this
// build handed out data-bus addresses inside the range the PSRAM heap
// occupies (0x3c810000 with the PSRAM pool ending at 0x3c890000): the
// flash pages replaced PSRAM pages under the heap and the next heap walk
// faulted. So the directory is copied into internal RAM and an image into
// a PSRAM buffer while its pulses are built.
#include "bundle.h"
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "bundle";

#define BUNDLE_SUBTYPE ((esp_partition_subtype_t)0x40)   // partitions.csv: discs, data, 0x40
#define MAX_ENTRIES 512

struct bundle_header {
    char magic[8];        // "BEEBDISC"
    uint32_t count;
    uint32_t reserved;
};

static const esp_partition_t *s_part;
static struct bundle_entry *s_entries;
static uint32_t s_count;
static uint8_t *s_image;

esp_err_t bundle_open(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, BUNDLE_SUBTYPE, NULL);
    if (s_part == NULL) {
        ESP_LOGW(TAG, "no discs partition");
        return ESP_ERR_NOT_FOUND;
    }
    struct bundle_header hdr;
    esp_err_t err = esp_partition_read(s_part, 0, &hdr, sizeof hdr);
    if (err != ESP_OK) {
        return err;
    }
    if (memcmp(hdr.magic, "BEEBDISC", 8) != 0 || hdr.count > MAX_ENTRIES) {
        ESP_LOGW(TAG, "discs partition holds no bundle (flash one: python tools/make_disc_bundle.py --flash COM3)");
        return ESP_ERR_NOT_FOUND;
    }
    struct bundle_entry *entries = malloc(hdr.count * sizeof *entries + 1);
    if (!entries) {
        return ESP_ERR_NO_MEM;
    }
    err = esp_partition_read(s_part, sizeof hdr, entries, hdr.count * sizeof *entries);
    if (err != ESP_OK) {
        free(entries);
        return err;
    }
    for (uint32_t i = 0; i < hdr.count; i++) {
        const struct bundle_entry *e = &entries[i];
        if ((size_t)e->offset + e->size > s_part->size || e->name[31] != 0) {
            ESP_LOGW(TAG, "bundle entry %u is out of bounds", (unsigned)i);
            free(entries);
            return ESP_ERR_INVALID_STATE;
        }
    }
    s_entries = entries;
    s_count = hdr.count;
    ESP_LOGI(TAG, "%u discs in the bundle", (unsigned)s_count);
    return ESP_OK;
}

int bundle_count(void)
{
    return (int)s_count;
}

const struct bundle_entry *bundle_entry(int i)
{
    if (i < 0 || (uint32_t)i >= s_count) {
        return NULL;
    }
    return &s_entries[i];
}

int bundle_find(const char *name)
{
    for (int i = 0; i < bundle_count(); i++) {
        if (strcmp(s_entries[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

const uint8_t *bundle_map(const struct bundle_entry *e)
{
    bundle_unmap();
    s_image = heap_caps_malloc(e->size, MALLOC_CAP_SPIRAM);
    if (!s_image) {
        ESP_LOGW(TAG, "no PSRAM for %s", e->name);
        return NULL;
    }
    if (esp_partition_read(s_part, e->offset, s_image, e->size) != ESP_OK) {
        ESP_LOGW(TAG, "reading %s failed", e->name);
        bundle_unmap();
        return NULL;
    }
    return s_image;
}

void bundle_unmap(void)
{
    if (s_image) {
        heap_caps_free(s_image);
        s_image = NULL;
    }
}
