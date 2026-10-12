// esp32-beeb: sound on the board (see sound_esp.h).
//
// The ES8311 is driven through esp_codec_dev as the Waveshare examples do
// (pins from their BSP header: MCLK 16, BCLK 9, WS 45, data out 8, data in
// 10, amplifier enable 46), I2S standard, mono, 16 bits at 31250 Hz: the
// chip's 250 kHz step rate divided by eight, so one output sample is the
// average of eight chip steps and no resampling is needed.
//
// The render task trails the machine by SOUND_LAG cycles: it renders the
// chip from where it left off to (machine cycle - lag), applying the bytes
// the machine queued with their cycles, 8192 cycles (4 ms, 128 samples) at
// a time, and the codec write blocks when the DMA buffers are full, which
// paces it. If the machine is behind real time the audio simply waits.
#include "sound_esp.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sn76489.h"

static const char *TAG = "sound";

// 32 kHz: the ES8311 driver's clock table has no 31250 Hz entry (the chip's
// 250 kHz step rate / 8), and with no entry the codec's clocks are left
// unset and it plays nothing. 250 chip steps make 32 output samples: each
// sample averages 7 or 8 steps.
#define SOUND_RATE 32000
#define SOUND_LAG 8000          // cycles behind the machine (4 ms)
#define SOUND_CHUNK 8192        // cycles rendered per step (128 samples)

#define I2S_MCLK GPIO_NUM_16
#define I2S_BCLK GPIO_NUM_9
#define I2S_WS GPIO_NUM_45
#define I2S_DOUT GPIO_NUM_8
#define I2S_DIN GPIO_NUM_10
#define PA_ENABLE GPIO_NUM_46

volatile uint32_t sound_machine_cycle;

struct sound_event {
    uint32_t cycle;
    uint8_t byte;
};
#define QUEUE_SIZE 1024   // a power of two
static struct sound_event s_queue[QUEUE_SIZE];
static volatile uint32_t s_qhead, s_qtail;

static struct sn76489 s_sn;
static uint32_t s_rendered;        // the cycle the chip has been rendered to
static uint32_t s_sn_phase;        // cycles into the current 8-cycle step
static int32_t s_dc_x, s_dc_y;     // the DC blocker's last input and output
static esp_codec_dev_handle_t s_speaker;
static volatile uint32_t s_writes, s_samples, s_drops, s_starved;

void sound_write(uint32_t cycle, uint8_t byte)
{
    uint32_t h = s_qhead;
    if (h - s_qtail >= QUEUE_SIZE) {
        s_drops++;
        return;
    }
    s_queue[h & (QUEUE_SIZE - 1)].cycle = cycle;
    s_queue[h & (QUEUE_SIZE - 1)].byte = byte;
    __sync_synchronize();
    s_qhead = h + 1;
    s_writes++;
}

void sound_set_volume(int volume)
{
    if (s_speaker) {
        esp_codec_dev_set_out_vol(s_speaker, volume < 0 ? 0 : volume > 100 ? 100 : volume);
    }
}

void sound_stats(uint32_t *writes, uint32_t *samples, uint32_t *drops, uint32_t *starved)
{
    *writes = s_writes;
    *samples = s_samples;
    *drops = s_drops;
    *starved = s_starved;
    s_writes = s_samples = s_drops = s_starved = 0;
}

// Renders the chip up to `cycle`, applying queued bytes on the way.
// Each eight cycles is one chip step and one 250 kHz sample; eight of those
// average to one output sample.
static void render_to(uint32_t cycle, int16_t *out, uint32_t *count)
{
    static int16_t steps[8];
    static uint32_t nsteps;
    static uint32_t need = 8, acc;   // steps for the current sample (7 or 8)
    while ((int32_t)(cycle - s_rendered) > 0) {
        uint32_t until = cycle;
        // The next queued byte lands before `until`: render up to it first.
        if (s_qtail != s_qhead) {
            uint32_t c = s_queue[s_qtail & (QUEUE_SIZE - 1)].cycle;
            if ((int32_t)(c - s_rendered) <= 0) {
                sn_write(&s_sn, s_queue[s_qtail & (QUEUE_SIZE - 1)].byte);
                __sync_synchronize();
                s_qtail++;
                continue;
            }
            if ((int32_t)(c - until) < 0) {
                until = c;
            }
        }
        uint32_t n = until - s_rendered;
        // Whole steps: the phase counts the cycles into the current one.
        uint32_t cycles = s_sn_phase + n;
        uint32_t whole = cycles / 8;
        s_sn_phase = cycles % 8;
        s_rendered = until;
        while (whole) {
            int32_t sum;
            if (nsteps == 0 && need == 8 && whole >= 8) {
                // A whole output sample: eight steps at once (host/sntest.c
                // checks it equals eight single steps summed).
                sum = sn_render8(&s_sn);
                whole -= 8;
            } else {
                uint32_t k = need - nsteps;
                if (k > whole) {
                    k = whole;
                }
                sn_render(&s_sn, steps + nsteps, k);
                nsteps += k;
                whole -= k;
                if (nsteps != need) {
                    continue;
                }
                sum = 0;
                for (uint32_t i = 0; i < need; i++) {
                    sum += steps[i];
                }
            }
            {
                // The chip's output is unipolar (0 to full scale); a DC
                // blocker (one pole, about 25 Hz) keeps it off the speaker.
                int32_t x = sum / (int32_t)need;
                int32_t y = x - s_dc_x + (s_dc_y * 1015) / 1024;
                s_dc_x = x;
                s_dc_y = y;
                out[(*count)++] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
                nsteps = 0;
                // The next sample's share of the 250 steps per 32 samples.
                acc += 250;
                need = acc / 32;
                acc %= 32;
            }
        }
    }
}

static void sound_task(void *arg)
{
    (void)arg;
    static int16_t out[SOUND_CHUNK / 64 + 16];
    s_rendered = sound_machine_cycle;
    for (;;) {
        uint32_t target = sound_machine_cycle - SOUND_LAG;
        if ((int32_t)(target - s_rendered) < SOUND_CHUNK) {
            s_starved++;
            vTaskDelay(1);
            continue;
        }
        uint32_t count = 0;
        render_to(s_rendered + SOUND_CHUNK, out, &count);
        if (count) {
            s_samples += count;
            esp_codec_dev_write(s_speaker, out, count * sizeof(int16_t));
        }
    }
}

esp_err_t sound_init(i2c_master_bus_handle_t i2c, int volume)
{
    sn_reset(&s_sn);
    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, &rx), TAG, "i2s channel");
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SOUND_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_DIN,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std_cfg), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std_cfg), TAG, "i2s rx");

    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_NUM_0, .tx_handle = tx, .rx_handle = rx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    audio_codec_i2c_cfg_t i2c_cfg = {.port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c};
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!data_if || !gpio_if || !ctrl_if) {
        ESP_LOGW(TAG, "codec interfaces failed");
        return ESP_FAIL;
    }
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = PA_ENABLE,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    if (!codec_if) {
        ESP_LOGW(TAG, "es8311 failed");
        return ESP_FAIL;
    }
    esp_codec_dev_cfg_t dev_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec_if, .data_if = data_if};
    s_speaker = esp_codec_dev_new(&dev_cfg);
    if (!s_speaker) {
        ESP_LOGW(TAG, "codec device failed");
        return ESP_FAIL;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 1, .channel_mask = 0, .sample_rate = SOUND_RATE, .mclk_multiple = 256,
    };
    if (esp_codec_dev_open(s_speaker, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "codec open failed");
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(s_speaker, volume);
    xTaskCreatePinnedToCore(sound_task, "sound", 4096, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "ES8311 at %d Hz, task on core 0", SOUND_RATE);
    return ESP_OK;
}
