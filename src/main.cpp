#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "bluetooth.h"
#include "led.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ── LEDs ─────────────────────────────────────────── */

#define FADE_STEP  5   /* brightness increment (%) */
#define FADE_MS   20   /* delay per step (ms)      */

static Led leds[] = {
    Led(PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0))),
    Led(PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1))),
    Led(PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2))),
};

static bool init_leds()
{
    for (int i = 0; i < ARRAY_SIZE(leds); i++) {
        if (!leds[i].init()) return false;
    }
    return true;
}

static void fade_led(int i)
{
    for (int b = 0;   b <= 100; b += FADE_STEP) { leds[i].set_brightness(b); k_msleep(FADE_MS); }
    for (int b = 100; b >= 0;   b -= FADE_STEP) { leds[i].set_brightness(b); k_msleep(FADE_MS); }
}

/* ── Sensor ───────────────────────────────────────── */

static const struct device *temp_dev;

static bool init_sensor()
{
    temp_dev = DEVICE_DT_GET_ANY(nordic_nrf_temp);
    return device_is_ready(temp_dev);
}

static int32_t read_temperature()
{
    struct sensor_value v;
    sensor_sample_fetch(temp_dev);
    sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &v);
    return v.val1;
}

/* ── Main ─────────────────────────────────────────── */

int main()
{
    LOG_INF("Starting thermometer...");

    if (!init_leds())   { LOG_ERR("LED init failed");    return 0; }
    if (!init_sensor())  { LOG_ERR("Sensor init failed"); return 0; }

    Bluetooth ble;
    if (ble.init()) { LOG_ERR("BLE init failed"); return 0; }

    int led = 0;
    while (1) {
        ble.update_temperature(read_temperature());
        fade_led(led);
        led = (led + 1) % ARRAY_SIZE(leds);
    }
}

