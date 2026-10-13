// esp32-beeb: touch and the PWR button (see input.h).
#include "input.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "catalog.h"
#include "console.h"
#include "sound_esp.h"
#undef M_PI   // qmi8658.h defines its own
#include "qmi8658.h"

static const char *TAG = "input";

#define TOUCH_ADDR 0x15
#define EXPANDER_ADDR 0x20
#define EXPANDER_REG_INPUT 0x00
#define EXPANDER_REG_CONFIG 0x03
#define PWR_BIT (1 << 4)           // EXIO4, high while held (FluidBox button.c)
#define POLL_MS 20
#define LONG_PRESS_MS 800
#define SWIPE_PX 60
#define SHORT_PRESS_MAX_MS 1500    // released before this: restart
#define MUTE_PRESS_MS 2000         // held this long: sound off/on (6 s is the power chip's power-off)
#define BOOT_GPIO GPIO_NUM_0      // the BOOT button pulls it low; in the menu it boots the highlighted disc
// A shake opens the disc menu: three samples past 2.5 g within 0.6 s.
// A tap or a knock is one spike; carrying the board is well under 2 g.
#define SHAKE_MPS2 (2.5f * 9.81f)
#define SHAKE_HITS 3
#define SHAKE_WINDOW_MS 600

// Keyboard matrix positions (row, column).
#define KEY_SPACE_ROW 6
#define KEY_SPACE_COL 2
#define KEY_RETURN_ROW 4
#define KEY_RETURN_COL 9

static i2c_master_dev_handle_t s_touch, s_expander;
static qmi8658_dev_t s_imu;
static int s_imu_ok;

// The mailbox: a small ring, one producer (this task), one consumer (the
// machine's loop).
struct key_event {
    uint8_t row, col, down;
};
#define MAILBOX 16
static struct key_event s_box[MAILBOX];
static volatile uint32_t s_box_head, s_box_tail;

static void post_key(int row, int col, int down)
{
    uint32_t h = s_box_head;
    if (h - s_box_tail >= MAILBOX) {
        return;
    }
    s_box[h & (MAILBOX - 1)].row = (uint8_t)row;
    s_box[h & (MAILBOX - 1)].col = (uint8_t)col;
    s_box[h & (MAILBOX - 1)].down = (uint8_t)down;
    __sync_synchronize();
    s_box_head = h + 1;
}

int input_take_key(int *row, int *col, int *down)
{
    uint32_t t = s_box_tail;
    if (t == s_box_head) {
        return 0;
    }
    *row = s_box[t & (MAILBOX - 1)].row;
    *col = s_box[t & (MAILBOX - 1)].col;
    *down = s_box[t & (MAILBOX - 1)].down;
    __sync_synchronize();
    s_box_tail = t + 1;
    return 1;
}

static void change_disc(int step)
{
    int n = catalog_count();
    if (n <= 0) {
        return;
    }
    char cur[32];
    console_get_disc(cur, sizeof cur);
    int i = cur[0] ? catalog_find(cur) : 0;
    if (i < 0) {
        i = 0;
    }
    i = (i + step + n) % n;
    const struct catalog_entry *e = catalog_entry(i);
    ESP_LOGI(TAG, "disc %d: %s", i, e->name);
    console_set_disc(e->name);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

// Menu mode's gestures, in a mailbox of their own.
static volatile int s_menu;
static struct { uint8_t kind; int16_t x, y; } s_gestures[8];
static volatile uint32_t s_gesture_head, s_gesture_tail;

void input_set_menu(int on)
{
    s_menu = on;
}

static void post_gesture(int kind, int x, int y)
{
    uint32_t h = s_gesture_head;
    if (h - s_gesture_tail >= 8) {
        return;
    }
    s_gestures[h & 7].kind = (uint8_t)kind;
    s_gestures[h & 7].x = (int16_t)x;
    s_gestures[h & 7].y = (int16_t)y;
    __sync_synchronize();
    s_gesture_head = h + 1;
}

int input_take_gesture(int *kind, int *x, int *y)
{
    uint32_t t = s_gesture_tail;
    if (t == s_gesture_head) {
        return 0;
    }
    *kind = s_gestures[t & 7].kind;
    *x = s_gestures[t & 7].x;
    *y = s_gestures[t & 7].y;
    __sync_synchronize();
    s_gesture_tail = t + 1;
    return 1;
}

// One poll of the touch controller: finger down, position.
static uint32_t s_polls, s_touch_errors, s_touch_down, s_button_errors;
static uint8_t s_last_touch[7], s_last_button;

static int read_touch(int *x, int *y)
{
    uint8_t reg = 0;
    uint8_t buf[7];
    if (i2c_master_transmit_receive(s_touch, &reg, 1, buf, sizeof buf, 20) != ESP_OK) {
        s_touch_errors++;
        return 0;
    }
    memcpy(s_last_touch, buf, sizeof buf);
    if (buf[2] & 0x0f) {
        s_touch_down++;
    }
    int fingers = buf[2] & 0x0f;
    *x = ((buf[3] & 0x0f) << 8) | buf[4];
    *y = ((buf[5] & 0x0f) << 8) | buf[6];
    return fingers > 0;
}

static int read_button(void)
{
    uint8_t reg = EXPANDER_REG_INPUT, v = 0;
    if (i2c_master_transmit_receive(s_expander, &reg, 1, &v, 1, 20) != ESP_OK) {
        s_button_errors++;
        return 0;
    }
    s_last_button = v;
    return (v & PWR_BIT) != 0;
}

static void input_task(void *arg)
{
    (void)arg;
    int was_down = 0, x0 = 0, y0 = 0, x = 0, y = 0;
    int64_t down_at = 0;
    int long_sent = 0, space_down = 0;
    int button_was = 0, agree = 0, mute_done = 0;
    int boot_was = 0, boot_agree = 0;
    int64_t button_at = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        int64_t now = esp_timer_get_time();
        static int64_t next_log;
        s_polls++;
        if (now >= next_log && (s_touch_down || s_touch_errors || s_button_errors)) {
            next_log = now + 1000000;
            ESP_LOGI(TAG, "polls %u, touch down %u errors %u (last %02x %02x %02x %02x %02x %02x %02x), expander %02x errors %u",
                     (unsigned)s_polls, (unsigned)s_touch_down, (unsigned)s_touch_errors,
                     s_last_touch[0], s_last_touch[1], s_last_touch[2], s_last_touch[3], s_last_touch[4],
                     s_last_touch[5], s_last_touch[6], s_last_button, (unsigned)s_button_errors);
            s_polls = s_touch_down = s_touch_errors = s_button_errors = 0;
        }

        // Touch: Space while down; a long press adds Return; a swipe on
        // release changes disc.
        if (s_touch) {
            int down = read_touch(&x, &y);
            if (down && !was_down) {
                x0 = x;
                y0 = y;
                down_at = now;
                long_sent = 0;
                space_down = !s_menu;
                if (space_down) {
                    post_key(KEY_SPACE_ROW, KEY_SPACE_COL, 1);
                }
            } else if (down && was_down) {
                if (!s_menu && !long_sent && now - down_at > LONG_PRESS_MS * 1000 && abs(x - x0) < SWIPE_PX && abs(y - y0) < SWIPE_PX) {
                    long_sent = 1;
                    post_key(KEY_RETURN_ROW, KEY_RETURN_COL, 1);
                    ESP_LOGI(TAG, "long press: Return");
                }
            } else if (!down && was_down) {
                if (space_down) {
                    post_key(KEY_SPACE_ROW, KEY_SPACE_COL, 0);
                    space_down = 0;
                }
                if (long_sent) {
                    post_key(KEY_RETURN_ROW, KEY_RETURN_COL, 0);
                }
                int dx = x - x0, dy = y - y0;
                int quick = now - down_at < 600 * 1000;
                if (quick && abs(dy) > SWIPE_PX && abs(dy) > abs(dx)) {
                    // The panel runs in its native portrait orientation, so
                    // the picture's long side is the touch controller's y.
                    ESP_LOGI(TAG, "swipe %s", dy > 0 ? "forward" : "back");
                    if (s_menu) {
                        post_gesture(dy > 0 ? INPUT_SWIPE_FORWARD : INPUT_SWIPE_BACK, 0, 0);
                    } else {
                        change_disc(dy > 0 ? 1 : -1);
                    }
                } else if (quick && abs(dx) > SWIPE_PX) {
                    ESP_LOGI(TAG, "swipe across");
                    if (s_menu) {
                        post_gesture(INPUT_SWIPE_ACROSS, 0, 0);
                    } else {
                        console_set_menu();
                        vTaskDelay(pdMS_TO_TICKS(50));
                        esp_restart();
                    }
                } else if (s_menu) {
                    // Picture coordinates: its top is at the panel's
                    // right-hand native edge (display.h), so its rows run
                    // against the controller's x.
                    int px = y0, py = 367 - x0;
                    ESP_LOGI(TAG, "tap at %d,%d (picture %d,%d)", x0, y0, px, py);
                    post_gesture(INPUT_TAP, px, py);
                } else if (quick) {
                    ESP_LOGI(TAG, "tap at %d,%d: Space", x0, y0);
                }
            }
            was_down = down;
        }

        // A shake: the accelerometer at the poll rate, gravity included (a
        // board at rest reads 1 g, so the threshold is on the magnitude).
        if (s_imu_ok) {
            static int hits;
            static int64_t first_hit;
            float ax = 0, ay = 0, az = 0;
            esp_err_t err = qmi8658_read_accel(&s_imu, &ax, &ay, &az);
            float m2 = ax * ax + ay * ay + az * az;
            if (err == ESP_OK && m2 > SHAKE_MPS2 * SHAKE_MPS2) {
                if (hits == 0 || now - first_hit > SHAKE_WINDOW_MS * 1000) {
                    hits = 0;
                    first_hit = now;
                }
                if (++hits >= SHAKE_HITS && !s_menu) {
                    ESP_LOGI(TAG, "shake: menu");
                    console_set_menu();
                    vTaskDelay(pdMS_TO_TICKS(50));
                    esp_restart();
                }
            }
        }

        // The BOOT button (GPIO 0), debounced: in the menu, a press boots
        // the highlighted disc.
        {
            int pressed = gpio_get_level(BOOT_GPIO) == 0;
            if (pressed == boot_was) {
                boot_agree = 0;
            } else if (++boot_agree >= 2) {
                boot_agree = 0;
                boot_was = pressed;
                if (pressed && s_menu) {
                    ESP_LOGI(TAG, "BOOT: select");
                    post_gesture(INPUT_SELECT, 0, 0);
                }
            }
        }

        // The PWR button, debounced: a short press restarts; held for 2 s it
        // toggles the sound at once, so the change is heard while held. The
        // AXP2101 cuts the power itself at 6 s.
        if (s_expander) {
            int pressed = read_button();
            if (pressed && button_was && !mute_done && now - button_at >= MUTE_PRESS_MS * 1000) {
                mute_done = 1;
                int muted = sound_toggle_mute();
                if (!muted && console_get_volume() == 0) {
                    console_set_volume(70);   // "on" from a kept volume of 0 means audible
                }
                ESP_LOGI(TAG, "PWR held: sound %s", muted ? "off" : "on");
            }
            if (pressed == button_was) {
                agree = 0;
            } else if (++agree >= 2) {
                agree = 0;
                button_was = pressed;
                if (pressed) {
                    button_at = now;
                    mute_done = 0;
                } else if (now - button_at < SHORT_PRESS_MAX_MS * 1000) {
                    ESP_LOGI(TAG, "PWR: restart");
                    vTaskDelay(pdMS_TO_TICKS(50));
                    esp_restart();
                }
            }
        }
    }
}

esp_err_t input_init(i2c_master_bus_handle_t i2c)
{
    if (i2c == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const gpio_config_t boot_cfg = {
        .pin_bit_mask = 1ull << BOOT_GPIO, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&boot_cfg);
    const i2c_device_config_t touch_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = TOUCH_ADDR, .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(i2c, &touch_cfg, &s_touch) == ESP_OK) {
        uint8_t reg = 0xa7, id = 0;   // chip id
        if (i2c_master_transmit_receive(s_touch, &reg, 1, &id, 1, 50) == ESP_OK) {
            ESP_LOGI(TAG, "touch controller id 0x%02x", id);
            // Keep it awake: auto sleep off.
            uint8_t off[2] = {0xfe, 0x01};
            i2c_master_transmit(s_touch, off, 2, 50);
        } else {
            ESP_LOGW(TAG, "no touch controller at 0x15");
            i2c_master_bus_rm_device(s_touch);
            s_touch = NULL;
        }
    }
    // The QMI8658, set up as FluidBox's imu.c does: a soft reset first (it
    // keeps its state through esp_restart, and without the reset every read
    // was zero), then 8 g at 250 Hz with the gyro on as well.
    for (int a = 0; a < 2 && !s_imu_ok; a++) {
        uint8_t addr = a ? QMI8658_ADDRESS_LOW : QMI8658_ADDRESS_HIGH;
        if (i2c_master_probe(i2c, addr, 50) != ESP_OK || qmi8658_init(&s_imu, i2c, addr) != ESP_OK) {
            continue;
        }
        if (qmi8658_write_register(&s_imu, 0x60, 0xb0) != ESP_OK) {   // soft reset
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        if (qmi8658_write_register(&s_imu, QMI8658_CTRL1, 0x60) == ESP_OK &&
            qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_8G) == ESP_OK &&
            qmi8658_set_accel_odr(&s_imu, QMI8658_ACCEL_ODR_250HZ) == ESP_OK &&
            qmi8658_set_gyro_range(&s_imu, QMI8658_GYRO_RANGE_512DPS) == ESP_OK &&
            qmi8658_set_gyro_odr(&s_imu, QMI8658_GYRO_ODR_250HZ) == ESP_OK &&
            qmi8658_enable_sensors(&s_imu, QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO) == ESP_OK) {
            qmi8658_set_accel_unit_mps2(&s_imu, true);
            s_imu_ok = 1;
            ESP_LOGI(TAG, "QMI8658 at 0x%02x: shake for the menu", addr);
        }
    }
    const i2c_device_config_t exp_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = EXPANDER_ADDR, .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(i2c, &exp_cfg, &s_expander) == ESP_OK) {
        uint8_t reg = EXPANDER_REG_CONFIG, cfg = 0xff;
        if (i2c_master_transmit_receive(s_expander, &reg, 1, &cfg, 1, 50) == ESP_OK) {
            if (!(cfg & PWR_BIT)) {
                uint8_t w[2] = {EXPANDER_REG_CONFIG, (uint8_t)(cfg | PWR_BIT)};   // EXIO4 an input, the rest as found
                i2c_master_transmit(s_expander, w, 2, 50);
            }
            ESP_LOGI(TAG, "PWR button on the expander (config 0x%02x)", cfg);
        } else {
            ESP_LOGW(TAG, "no IO expander at 0x20");
            i2c_master_bus_rm_device(s_expander);
            s_expander = NULL;
        }
    }
    // Above the render task (5), which keeps core 0 busy almost all the time:
    // at a lower priority the polls ran late or not at all.
    if (xTaskCreatePinnedToCore(input_task, "input", 3072, NULL, 7, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the input task");
    }
    return ESP_OK;
}
