// esp32-beeb: the disc bundle partition (see bundle.h and
// tools/make_disc_bundle.py).
#include "bundle.h"
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "bundle";

#define BUNDLE_SUBTYPE ((esp_partition_subtype_t)0x40)   // partitions.csv: discs, data, 0x40

struct bundle_header {
    char magic[8];        // "BEEBDISC"
    uint32_t count;
    uint32_t reserved;
};

// The directory: the header and up to 2000 entries fit in the first 96 KB.
#define DIR_BYTES (96 * 1024)

static const esp_partition_t *s_part;
static esp_partition_mmap_handle_t s_dir_handle, s_image_handle;
static const struct bundle_header *s_hdr;
static const struct bundle_entry *s_entries;
static int s_image_mapped;

esp_err_t bundle_open(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, BUNDLE_SUBTYPE, NULL);
    if (s_part == NULL) {
        ESP_LOGW(TAG, "no discs partition");
        return ESP_ERR_NOT_FOUND;
    }
    const void *base;
    esp_err_t err = esp_partition_mmap(s_part, 0, DIR_BYTES, ESP_PARTITION_MMAP_DATA, &base, &s_dir_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mapping the bundle directory failed: %s", esp_err_to_name(err));
        return err;
    }
    const struct bundle_header *hdr = base;
    if (memcmp(hdr->magic, "BEEBDISC", 8) != 0 || hdr->count > 2000) {
        ESP_LOGW(TAG, "discs partition holds no bundle (flash one: python tools/make_disc_bundle.py --flash COM3)");
        return ESP_ERR_NOT_FOUND;
    }
    const struct bundle_entry *entries = (const struct bundle_entry *)((const uint8_t *)base + sizeof *hdr);
    for (uint32_t i = 0; i < hdr->count; i++) {
        const struct bundle_entry *e = &entries[i];
        if ((size_t)e->offset + e->size > s_part->size || e->name[31] != 0) {
            ESP_LOGW(TAG, "bundle entry %u is out of bounds", (unsigned)i);
            return ESP_ERR_INVALID_STATE;
        }
    }
    s_hdr = hdr;
    s_entries = entries;
    ESP_LOGI(TAG, "%u discs in the bundle", (unsigned)hdr->count);
    return ESP_OK;
}

int bundle_count(void)
{
    return s_hdr ? (int)s_hdr->count : 0;
}

const struct bundle_entry *bundle_entry(int i)
{
    if (!s_hdr || i < 0 || (uint32_t)i >= s_hdr->count) {
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
    // The mapping is in 64 KB pages: map from the page holding the image.
    uint32_t page = e->offset & ~0xffffu;
    const void *base;
    esp_err_t err = esp_partition_mmap(s_part, page, e->offset - page + e->size, ESP_PARTITION_MMAP_DATA,
                                       &base, &s_image_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mapping %s failed: %s", e->name, esp_err_to_name(err));
        return NULL;
    }
    s_image_mapped = 1;
    return (const uint8_t *)base + (e->offset - page);
}

void bundle_unmap(void)
{
    if (s_image_mapped) {
        esp_partition_munmap(s_image_handle);
        s_image_mapped = 0;
    }
}
