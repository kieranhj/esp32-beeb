// esp32-beeb: disc images on the microSD card (see sdcard.h).
#include "sdcard.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";

// Waveshare's BSP for this board: 1-bit SDMMC through the GPIO matrix, the
// card's D3 (its SPI chip select) on the TCA9554's EXIO7.
#define SD_CLK GPIO_NUM_2
#define SD_CMD GPIO_NUM_1
#define SD_D0 GPIO_NUM_3
#define EXPANDER_ADDR 0x20
#define EXPANDER_REG_OUTPUT 0x01
#define EXPANDER_REG_CONFIG 0x03
#define SD_D3_BIT (1 << 7)

#define MOUNT_POINT "/sd"
#define MAX_FILES 128
#define MAX_IMAGE (2 * 80 * 10 * 256)   // a double sided 80 track .dsd

static sdmmc_card_t *s_card;
static struct sdcard_file *s_files;
static int s_count;

// EXIO7 an output, driven high: D3 high at power up selects SD mode (low
// would put the card in SPI mode). The other pins keep their direction.
static void d3_high(i2c_master_bus_handle_t i2c)
{
    if (i2c == NULL) {
        return;
    }
    i2c_master_dev_handle_t dev;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = EXPANDER_ADDR,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(i2c, &cfg, &dev) != ESP_OK) {
        return;
    }
    uint8_t reg = EXPANDER_REG_OUTPUT, out = 0xff, conf = 0xff;
    if (i2c_master_transmit_receive(dev, &reg, 1, &out, 1, 50) == ESP_OK) {
        reg = EXPANDER_REG_CONFIG;
        if (i2c_master_transmit_receive(dev, &reg, 1, &conf, 1, 50) == ESP_OK) {
            uint8_t w[2] = {EXPANDER_REG_OUTPUT, (uint8_t)(out | SD_D3_BIT)};
            i2c_master_transmit(dev, w, 2, 50);
            w[0] = EXPANDER_REG_CONFIG;
            w[1] = (uint8_t)(conf & ~SD_D3_BIT);
            i2c_master_transmit(dev, w, 2, 50);
        }
    }
    i2c_master_bus_rm_device(dev);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static int is_disc_name(const char *n)
{
    size_t len = strlen(n);
    return len > 4 && len < sizeof s_files[0].name &&
           (strcasecmp(n + len - 4, ".ssd") == 0 || strcasecmp(n + len - 4, ".dsd") == 0);
}

static void scan(const char *dir, int in_discs)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        return;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && s_count < MAX_FILES) {
        if (de->d_type == DT_DIR || !is_disc_name(de->d_name)) {
            continue;
        }
        char path[300];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || st.st_size <= 0 || st.st_size > MAX_IMAGE) {
            continue;
        }
        struct sdcard_file *f = &s_files[s_count++];
        memset(f, 0, sizeof *f);
        memcpy(f->name, de->d_name, strlen(de->d_name));   // is_disc_name checked the length
        f->size = (uint32_t)st.st_size;
        f->in_discs = (uint8_t)in_discs;
    }
    closedir(d);
}

esp_err_t sdcard_open(i2c_master_bus_handle_t i2c)
{
    d3_high(i2c);
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;   // 20 MHz: a few hundred KB is read once
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_CLK;
    slot.cmd = SD_CMD;
    slot.d0 = SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 0,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no card (%s)", esp_err_to_name(err));
        s_card = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    s_files = heap_caps_calloc(MAX_FILES, sizeof *s_files, MALLOC_CAP_SPIRAM);
    if (s_files == NULL) {
        sdcard_close();
        return ESP_ERR_NO_MEM;
    }
    scan(MOUNT_POINT, 0);
    scan(MOUNT_POINT "/discs", 1);
    ESP_LOGI(TAG, "card %s, %llu MB, %d disc images", s_card->cid.name,
             (unsigned long long)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024), s_count);
    return ESP_OK;
}

int sdcard_count(void)
{
    return s_count;
}

const struct sdcard_file *sdcard_file(int i)
{
    return (i >= 0 && i < s_count) ? &s_files[i] : NULL;
}

uint8_t *sdcard_read(int i, uint32_t *size)
{
    const struct sdcard_file *f = sdcard_file(i);
    if (f == NULL || s_card == NULL) {
        return NULL;
    }
    char path[64];
    snprintf(path, sizeof path, "%s%s/%s", MOUNT_POINT, f->in_discs ? "/discs" : "", f->name);
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        ESP_LOGW(TAG, "cannot open %s", path);
        return NULL;
    }
    uint8_t *buf = heap_caps_malloc(f->size, MALLOC_CAP_SPIRAM);
    size_t got = buf ? fread(buf, 1, f->size, fp) : 0;
    fclose(fp);
    if (buf == NULL || got != f->size) {
        ESP_LOGW(TAG, "short read of %s (%u of %lu)", path, (unsigned)got, (unsigned long)f->size);
        heap_caps_free(buf);
        return NULL;
    }
    *size = f->size;
    return buf;
}

void sdcard_close(void)
{
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
        s_card = NULL;
    }
}
