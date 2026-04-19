#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "bluetooth.h"
#include "led.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ── Global State ─────────────────────────────────── */
volatile uint8_t current_alert_level = 0;

void on_alert_received(uint8_t level) {
    current_alert_level = level;
}

/* ── LEDs ─────────────────────────────────────────── */
#define FADE_STEP  5   
#define FADE_MS   20   

static Led leds[] = {
    Led(PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0))),
    Led(PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1))),
    Led(PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2))),
};

static bool init_leds() {
    for (int i = 0; i < ARRAY_SIZE(leds); i++) {
        if (!leds[i].init()) return false;
    }
    return true;
}

static void set_leds_brightness(uint8_t brightness) {
    for (int i = 0; i < ARRAY_SIZE(leds); i++) {
        leds[i].set_brightness(brightness);
    }
}

static void fade_led(int ledIdx) {
    for (int b = 0; b <= 100; b += FADE_STEP) { 
        if (current_alert_level > 0) return; // Abort fade if alerting!
        leds[ledIdx].set_brightness(b); k_msleep(FADE_MS); 
    }
    for (int b = 100; b >= 0; b -= FADE_STEP) { 
        if (current_alert_level > 0) return; // Abort fade if alerting!
        leds[ledIdx].set_brightness(b); k_msleep(FADE_MS); 
    }
}

/* ── Temperature Sensor ───────────────────────────────────────── */
static const struct device *temp_dev;
// Initialize the Nordic temperature sensor
static bool init_sensor() {
    temp_dev = DEVICE_DT_GET_ANY(nordic_nrf_temp);
    return device_is_ready(temp_dev);
}

static int32_t read_temperature() {
    struct sensor_value v;
    sensor_sample_fetch(temp_dev);
    sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &v);
    return v.val1;
}

/* ── Main ─────────────────────────────────────────── */
int main() {
    LOG_INF("Starting Two-Way Tracker...");

    if (!init_leds())   { LOG_ERR("LED init failed");    return 0; }
    if (!init_sensor()) { LOG_ERR("Sensor init failed"); return 0; }

    Bluetooth ble;
    ble.set_alert_callback(on_alert_received);
    if (ble.init()) { LOG_ERR("BLE init failed"); return 0; }

    int led_idx = 0;
    while (1) {
        ble.update_temperature(read_temperature());

        // STATE MACHINE: Normal mode vs Alert Mode
        if (current_alert_level == 1) {
            // STROBE ALL LEDs!
            set_leds_brightness(100);
            k_msleep(100);
            set_leds_brightness(0);
            k_msleep(100);
            
        } else if (current_alert_level == 2) {
                // HIGH ALERT: STROBE FASTER
                set_leds_brightness(100);
                k_msleep(50);
                set_leds_brightness(0);
                k_msleep(50);
        } else {
            // Normal idle fade
            fade_led(led_idx);
            led_idx = (led_idx + 1) % ARRAY_SIZE(leds);
            k_msleep(500); // Wait a half-second between LED sweeps
        }
    }
}