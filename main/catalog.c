// esp32-beeb: the bundle and the card as one list of discs (see catalog.h).
#include "catalog.h"
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "bundle.h"
#include "sdcard.h"

#define MAX_ENTRIES 256

static struct catalog_entry *s_entries;
static int s_count;
static uint8_t *s_sd_image;   // a card image read for booting

static int sides_of(const char *name)
{
    size_t len = strlen(name);
    return (len > 4 && strcasecmp(name + len - 4, ".dsd") == 0) ? 2 : 1;
}

void catalog_build(void)
{
    if (s_entries == NULL) {
        s_entries = heap_caps_calloc(MAX_ENTRIES, sizeof *s_entries, MALLOC_CAP_SPIRAM);
        if (s_entries == NULL) {
            return;
        }
    }
    s_count = 0;
    for (int i = 0; i < bundle_count() && s_count < MAX_ENTRIES; i++) {
        const struct bundle_entry *b = bundle_entry(i);
        struct catalog_entry *e = &s_entries[s_count++];
        memcpy(e->name, b->name, sizeof e->name);
        e->name[sizeof e->name - 1] = 0;
        e->size = b->size;
        e->sides = b->sides;
        e->on_sd = 0;
        e->index = (int16_t)i;
    }
    for (int i = 0; i < sdcard_count(); i++) {
        const struct sdcard_file *f = sdcard_file(i);
        int j = catalog_find(f->name);
        if (j < 0) {
            if (s_count >= MAX_ENTRIES) {
                continue;
            }
            j = s_count++;
        }
        struct catalog_entry *e = &s_entries[j];
        memcpy(e->name, f->name, sizeof e->name);
        e->size = f->size;
        e->sides = (uint8_t)sides_of(f->name);
        e->on_sd = 1;
        e->index = (int16_t)i;
    }
}

int catalog_count(void)
{
    return s_count;
}

const struct catalog_entry *catalog_entry(int i)
{
    return (i >= 0 && i < s_count) ? &s_entries[i] : NULL;
}

int catalog_find(const char *name)
{
    for (int i = 0; i < s_count; i++) {
        if (strcasecmp(s_entries[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

const uint8_t *catalog_load(int i)
{
    const struct catalog_entry *e = catalog_entry(i);
    if (e == NULL) {
        return NULL;
    }
    if (!e->on_sd) {
        return bundle_map(bundle_entry(e->index));
    }
    uint32_t size;
    catalog_release();
    s_sd_image = sdcard_read(e->index, &size);
    return s_sd_image;
}

void catalog_release(void)
{
    bundle_unmap();
    if (s_sd_image) {
        heap_caps_free(s_sd_image);
        s_sd_image = NULL;
    }
}
