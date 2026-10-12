// esp32-beeb: sound on the board: the SN76489 model rendered in lockstep
// with the machine's cycle count on core 0 and sent to the ES8311 over I2S.
#ifndef SOUND_ESP_H
#define SOUND_ESP_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// Codec, I2S and the render task. The I2C bus carries the codec's control.
esp_err_t sound_init(i2c_master_bus_handle_t i2c, int volume);

// A byte landed on the chip at machine cycle `cycle` (core 1, from the
// system VIA). Lock free: one producer, one consumer.
void sound_write(uint32_t cycle, uint8_t byte);

// The machine's current cycle, set by the pacing loop after each slice;
// the render task stays a few ms behind it.
extern volatile uint32_t sound_machine_cycle;

// Speaker volume, 0 to 100 (persisted by the console).
void sound_set_volume(int volume);

// Counters since the last call: bytes written, output samples, bytes the
// queue could not take, times the task found nothing to render.
void sound_stats(uint32_t *writes, uint32_t *samples, uint32_t *drops, uint32_t *starved);

#endif
