#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "bluetooth.h"
#include "led.h"
#include "imu.h" // <-- NEW: Include your IMU class

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ── Global Objects ───────────────────────────────── */
Bluetooth ble;
Imu board_imu; // <-- NEW: Instantiate the IMU

/* ── IPC: Message Queue ───────────────────────────── */
struct system_state_msg {
    uint8_t alert_level;
    uint8_t brightness;
};

K_MSGQ_DEFINE(state_msgq, sizeof(struct system_state_msg), 10, 4);

/* ── Bluetooth Callbacks ──────────────────────────── */
void on_alert_received(uint8_t level) {
    struct system_state_msg msg;
    msg.alert_level = level;
    msg.brightness = bt_get_brightness(); 
    k_msgq_put(&state_msgq, &msg, K_NO_WAIT);
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
    // A quick way to peek at the queue without removing the message
    struct system_state_msg peek_msg;
    
    for (int b = 0; b <= 100; b += FADE_STEP) { 
        if (k_msgq_peek(&state_msgq, &peek_msg) == 0 && peek_msg.alert_level > 0) return;
        if (bt_get_brightness() > 0) return;
        leds[ledIdx].set_brightness(b); k_msleep(FADE_MS); 
    }
    for (int b = 100; b >= 0; b -= FADE_STEP) { 
        if (k_msgq_peek(&state_msgq, &peek_msg) == 0 && peek_msg.alert_level > 0) return;
        if (bt_get_brightness() > 0) return;
        leds[ledIdx].set_brightness(b); k_msleep(FADE_MS); 
    }
}

/* ── Temperature Sensor ───────────────────────────── */
static const struct device *temp_dev;

static bool init_temp_sensor() {
    temp_dev = DEVICE_DT_GET_ANY(nordic_nrf_temp);
    return device_is_ready(temp_dev);
}

static int32_t read_temperature() {
    struct sensor_value v;
    sensor_sample_fetch(temp_dev);
    sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &v);
    return v.val1;
}

/* ── Thread 1: Sensor & Bluetooth Updater ─────────── */
void sensor_thread_func(void *arg1, void *arg2, void *arg3) {
    while (1) {
        if (device_is_ready(temp_dev)) {
            ble.update_temperature(read_temperature());
        }
        k_msleep(2000);
    }
}
K_THREAD_DEFINE(sensor_tid, 1024, sensor_thread_func, NULL, NULL, NULL, 7, 0, 0);

/* ── Thread 2: IMU Accelerometer Reader ───────────── */
void imu_thread_func(void *arg1, void *arg2, void *arg3) {
    while (1) {
        double ax = 0, ay = 0, az = 0;
        board_imu.read_acceleration(ax, ay, az);
        LOG_INF("IMU | X: %7.2f | Y: %7.2f | Z: %7.2f", ax, ay, az);
        k_msleep(50);
    }
}
K_THREAD_DEFINE(imu_tid, 2048, imu_thread_func, NULL, NULL, NULL, 7, 0, 0);

/* ── Thread 3: LED State Machine ──────────────────── */
void led_thread_func(void *arg1, void *arg2, void *arg3) {
    int led_idx = 0;
    struct system_state_msg current_state = {0, 0}; 

    while (1) {
        struct system_state_msg new_msg;
        if (k_msgq_get(&state_msgq, &new_msg, K_NO_WAIT) == 0) {
            current_state = new_msg;
        }

        current_state.brightness = bt_get_brightness();

        if (current_state.alert_level == 1) {
            set_leds_brightness(100); k_msleep(100);
            set_leds_brightness(0);   k_msleep(100);
        } else if (current_state.alert_level == 2) {
            set_leds_brightness(100); k_msleep(50);
            set_leds_brightness(0);   k_msleep(50);
        } else {
            if (current_state.brightness > 0) {
                set_leds_brightness(current_state.brightness);
                k_msleep(100);
            } else {
                fade_led(led_idx);
                led_idx = (led_idx + 1) % ARRAY_SIZE(leds);
                k_msleep(500);
            }
        }
    }
}
K_THREAD_DEFINE(led_tid, 1024, led_thread_func, NULL, NULL, NULL, 7, 0, 0);

/* ── Main ─────────────────────────────────────────── */
int main() {
    LOG_INF("Starting RTOS with I2C IMU...");

    if (!init_leds())        { LOG_ERR("LED init failed");    return 0; }
    if (!init_temp_sensor()) { LOG_ERR("Temp init failed");   return 0; }
    
    // Initialize our new I2C hardware
    if (!board_imu.init())   { LOG_ERR("IMU init failed");    return 0; }

    ble.set_alert_callback(on_alert_received);
    if (ble.init())          { LOG_ERR("BLE init failed");    return 0; }

    return 0;
}