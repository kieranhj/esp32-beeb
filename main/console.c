// esp32-beeb: the serial console (see console.h). It reads the USB serial
// JTAG port directly through its driver, so it works whichever port the
// IDF console is on; output goes through printf like the STATS lines.
#include "console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "catalog.h"
#include "sound_esp.h"

static const char *TAG = "console";

void console_get_disc(char *out, unsigned len)
{
    out[0] = 0;
    nvs_handle_t h;
    if (nvs_open("beeb", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t n = len;
    if (nvs_get_str(h, "disc", out, &n) != ESP_OK) {
        out[0] = 0;
    }
    nvs_close(h);
}

int console_get_volume(void)
{
    nvs_handle_t h;
    int32_t vol = 70;
    if (nvs_open("beeb", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, "vol", &vol);
        nvs_close(h);
    }
    return (int)vol;
}

static void set_volume(int vol)
{
    nvs_handle_t h;
    if (nvs_open("beeb", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "vol", vol);
        nvs_commit(h);
        nvs_close(h);
    }
    sound_set_volume(vol);
}

void console_set_disc(const char *name)
{
    nvs_handle_t h;
    if (nvs_open("beeb", NVS_READWRITE, &h) != ESP_OK) {
        printf("console: nvs open failed\n");
        return;
    }
    nvs_set_str(h, "disc", name);
    nvs_commit(h);
    nvs_close(h);
}

static void command(char *line)
{
    char *sp = strchr(line, ' ');
    const char *arg = "";
    if (sp) {
        *sp = 0;
        arg = sp + 1;
        while (*arg == ' ') {
            arg++;
        }
    }
    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        printf("console: help | list | boot <disc> | vol <0-100> | reset\n");
    } else if (strcmp(line, "list") == 0) {
        char cur[32];
        console_get_disc(cur, sizeof cur);
        printf("console: %d discs (chosen: %s)\n", catalog_count(), cur[0] ? cur : "none");
        for (int i = 0; i < catalog_count(); i++) {
            const struct catalog_entry *e = catalog_entry(i);
            printf("  %s %lu bytes%s%s\n", e->name, (unsigned long)e->size, e->sides == 2 ? " two sides" : "",
                   e->on_sd ? " (card)" : "");
        }
    } else if (strcmp(line, "boot") == 0) {
        if (catalog_find(arg) < 0) {
            printf("console: no disc called '%s' (list shows them)\n", arg);
            return;
        }
        console_set_disc(arg);
        printf("console: booting %s\n", arg);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else if (strcmp(line, "vol") == 0) {
        int vol = atoi(arg);
        set_volume(vol);
        printf("console: volume %d\n", vol);
    } else if (strcmp(line, "reset") == 0) {
        printf("console: reset\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else if (line[0]) {
        printf("console: unknown command '%s' (help lists them)\n", line);
    }
}

static void console_task(void *arg)
{
    (void)arg;
    char line[96];
    unsigned n = 0;
    for (;;) {
        uint8_t c;
        int got = usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY);
        if (got <= 0) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (n) {
                line[n] = 0;
                command(line);
                n = 0;
            }
        } else if (n < sizeof line - 1 && c >= 0x20) {
            line[n++] = (char)c;
        }
    }
}

void console_start(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "usb serial jtag driver: %s", esp_err_to_name(err));
        return;
    }
    if (xTaskCreatePinnedToCore(console_task, "console", 3072, NULL, 2, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the console task");
    }
}
