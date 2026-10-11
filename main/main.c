// esp32-beeb: skeleton entry point.
//
// Nothing is emulated yet. This exists so the project builds, flashes and
// prints a banner before Phase 0 starts. See PLAN.md.

#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static const char *TAG = "beeb";

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "esp32-beeb skeleton starting");
    ESP_LOGI(TAG, "cores=%d rev=%d psram=%u bytes",
             chip.cores, chip.revision, (unsigned)esp_psram_get_size());
    ESP_LOGI(TAG, "internal heap free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "dma heap free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
