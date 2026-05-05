/**
 * @file main.cpp
 * @brief HaykulTracker firmware entry point.
 *
 * Wires all hardware + BLE modules and implements the system-level state
 * machine:
 *
 *  Module          Responsibility
 *  ──────────────  ──────────────────────────────────────────────────────
 *  BleManager      GATT server: buzzer/volume/alert-config writes,
 *                  IMU / button-event / alert-status notifications
 *  ImuBma400       Accelerometer reads at 10 Hz (via imu_thread)
 *  Buzzer          PWM buzzer with volume control
 *  NfcManager      T2T NFC tag: BLE OOB + AAR records for tap-to-pair
 *  ButtonManager   Double-click → notify phone; long-press → silence buzzer
 *  LedManager      Status LED state machine (blink / solid / off)
 *  BatteryMonitor  SAADC VDDHDIV5 battery level, triggers low-battery LED
 *
 * LED State Machine (priority order — highest first):
 *   BLINK_SLOW  — battery low (< 20%)
 *   BLINK_FAST  — scanning / not connected
 *   ON_SOLID    — BLE central connected
 *
 * Disconnect Alert:
 *   When the Android app writes alert-config with a non-zero timeout T,
 *   a countdown starts on BLE disconnect.  If the phone does not reconnect
 *   within T seconds the buzzer rings and alert-status 0x01 is sent on
 *   reconnection (or buffered until next connect).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "ble_manager.h"
#include "imu_bma400.h"
#include "buzzer.h"
#include "nfc_manager.h"
#include "button_manager.h"
#include "led_manager.h"
#include "battery_monitor.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ── Global Module Instances ─────────────────────────────────────────────── */

static BleManager      ble;
static ImuBma400       imu;
static Buzzer          buzzer;
static NfcManager      nfc;
static ButtonManager   button;
static LedManager      led;
static BatteryMonitor  battery;

/* ── Alert Countdown State ───────────────────────────────────────────────── */

/** Disconnect-alert timeout configured by the Android app (0 = disabled). */
static uint8_t  alert_timeout_secs = 0U;
/** True when the disconnect-alert countdown is currently running. */
static bool     alert_pending      = false;
/** True when the alert has fired and needs to be reported on next connect. */
static bool     alert_fired        = false;

static void alert_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(alert_work, alert_work_fn);

/**
 * @brief Delayed-work callback: fires when the disconnect-alert expires.
 *
 * Rings the buzzer locally so the user knows the phone is far away.
 * On next BLE reconnect, alert-status 0x01 is sent to the phone.
 */
static void alert_work_fn(struct k_work *work)
{
    ARG_UNUSED(work);
    alert_pending = false;
    alert_fired   = true;
    LOG_INF("Disconnect alert timeout expired — ringing buzzer");
    buzzer.enable(true);
}

/* ── BLE Event Callbacks ─────────────────────────────────────────────────── */

/**
 * @brief Called when the Android app writes to the Buzzer characteristic.
 * @param enable true = buzzer on, false = buzzer off.
 */
static void on_buzzer_command(bool enable)
{
    LOG_INF("BLE buzzer command: %s", enable ? "ON" : "OFF");
    buzzer.enable(enable);
}

/**
 * @brief Called when the Android app writes to the Volume characteristic.
 * @param percent Volume level 0–100.  Applied immediately and persists while
 *                the buzzer is active.
 */
static void on_volume_command(uint8_t percent)
{
    LOG_INF("BLE volume command: %d%%", percent);
    buzzer.set_volume(percent);
}

/**
 * @brief Called when the Android app writes to the Alert Config characteristic.
 * @param timeout_secs Seconds to wait after disconnect before ringing.
 *                     0 = disable proximity alert.
 */
static void on_alert_config(uint8_t timeout_secs)
{
    LOG_INF("Alert config: disconnect timeout = %d s", timeout_secs);
    alert_timeout_secs = timeout_secs;
    /* Cancel any in-flight countdown; the new value takes effect on the
     * next disconnect event. */
    if (timeout_secs == 0U) {
        k_work_cancel_delayable(&alert_work);
        alert_pending = false;
    }
}

/* ── Button Event Callbacks ──────────────────────────────────────────────── */

/**
 * @brief Called on double-click of the user button (SW1).
 *
 * Sends button-event code 0x01 to the phone, which causes it to ring/vibrate.
 * This lets the user locate a misplaced phone by pressing the tracker button.
 */
static void on_button_double_click()
{
    LOG_INF("Button double-click: notifying phone to ring");
    ble.notify_button_event(0x01U);
}

/**
 * @brief Called on long-press (≥ 3 s) of the user button (SW1).
 *
 * Silences the local buzzer (e.g. the disconnect-alert or the Android-triggered
 * ring).  Does not send a BLE notification — the phone sees the buzzer go quiet
 * when the RSSI recovers.
 */
static void on_button_long_press()
{
    LOG_INF("Button long-press: silencing buzzer");
    buzzer.enable(false);
    /* Clear any pending alert so it doesn't re-fire. */
    alert_fired = false;
    k_work_cancel_delayable(&alert_work);
    alert_pending = false;
}

/* ── BLE Connection / Disconnection Hooks ────────────────────────────────── */

/**
 * @brief Shared handler: called from BleManager's connected callback bridge.
 *
 * Called via a thin wrapper in BleManager (or polled from main loop).
 * Updates LED state and reports any pending alert.
 */
static void handle_connected()
{
    LOG_INF("Phone connected");

    /* Cancel the disconnect-alert countdown if it was running. */
    if (alert_pending) {
        k_work_cancel_delayable(&alert_work);
        alert_pending = false;
    }

    /* If the alert fired while the phone was away, report it now. */
    if (alert_fired) {
        alert_fired = false;
        ble.notify_alert_status(0x01U);
        LOG_INF("Reported deferred proximity alert to phone");
    }

    /* Update LED: connected (unless battery is low). */
    if (!battery.is_low()) {
        led.set_mode(LedManager::Mode::ON_SOLID);
    }
}

/**
 * @brief Shared handler: called from BleManager's disconnected callback bridge.
 *
 * Starts the disconnect-alert countdown if configured.
 */
static void handle_disconnected()
{
    LOG_INF("Phone disconnected");

    /* Start alert countdown if configured. */
    if (alert_timeout_secs > 0U) {
        alert_pending = true;
        k_work_schedule(&alert_work, K_SECONDS(alert_timeout_secs));
        LOG_INF("Alert countdown started: %d s", alert_timeout_secs);
    }

    /* Update LED: scanning (unless battery is low). */
    if (!battery.is_low()) {
        led.set_mode(LedManager::Mode::BLINK_FAST);
    }
}

/* ── IMU Thread ──────────────────────────────────────────────────────────── */

/**
 * @brief Continuously reads BMA400 data and notifies the BLE central at ~10 Hz.
 * @param arg1 Unused.
 * @param arg2 Unused.
 * @param arg3 Unused.
 */
static void imu_thread_func(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    uint8_t imu_data[6];
    while (1) {
        int len = imu.read_data(imu_data, sizeof(imu_data));
        if (len > 0 && ble.is_connected()) {
            ble.notify_imu_data(imu_data, (uint16_t)len);
        }
        k_msleep(100); /* 10 Hz update rate */
    }
}

K_THREAD_DEFINE(imu_tid, 1024, imu_thread_func, NULL, NULL, NULL, 7, 0, 0);

/* ── Battery + LED Polling Thread ────────────────────────────────────────── */

/**
 * @brief Periodically reads battery level and updates BLE + LED.
 *
 * Runs every 30 seconds.  When the battery drops below the low threshold:
 *   - The status LED switches to slow-blink.
 *   - The BLE Battery Service characteristic is updated.
 * When battery recovers above threshold (e.g. after charging starts), the LED
 * returns to connection-state-appropriate mode.
 */
static void battery_thread_func(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (1) {
        k_msleep(30000); /* 30 s interval */

        if (!battery.is_ready()) {
            continue;
        }

        uint8_t level = battery.read_level_percent();
        ble.update_battery_level(level);
        LOG_INF("Battery: %d%%", level);

        if (battery.is_low()) {
            /* Low battery — override LED regardless of connection state. */
            led.set_mode(LedManager::Mode::BLINK_SLOW);
        } else {
            /* Restore LED to connection-appropriate state. */
            if (ble.is_connected()) {
                led.set_mode(LedManager::Mode::ON_SOLID);
            } else {
                led.set_mode(LedManager::Mode::BLINK_FAST);
            }
        }
    }
}

K_THREAD_DEFINE(battery_tid, 2048, battery_thread_func, NULL, NULL, NULL, 8, 0, 0);

/* ── BLE Connection Polling Thread ──────────────────────────────────────── */

/**
 * @brief Detects BLE connect/disconnect transitions and calls the handlers.
 *
 * Because BleManager exposes `is_connected()` as a simple bool (rather than
 * a callback), this thread polls it at 500 ms resolution to drive the LED
 * state machine and the disconnect-alert countdown.
 */
static void conn_poll_thread_func(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    bool last_connected = false;
    while (1) {
        bool now_connected = ble.is_connected();
        if (now_connected && !last_connected) {
            handle_connected();
        } else if (!now_connected && last_connected) {
            handle_disconnected();
        }
        last_connected = now_connected;
        k_msleep(500);
    }
}

K_THREAD_DEFINE(conn_poll_tid, 1024, conn_poll_thread_func, NULL, NULL, NULL, 8, 0, 0);

/* ── main() ──────────────────────────────────────────────────────────────── */

int main()
{
    LOG_INF("HaykulTracker Firmware Starting...");

    /* --- Buzzer --- */
    if (!buzzer.init()) {
        LOG_ERR("Buzzer init failed");
    }

    /* --- IMU --- */
    if (!imu.init()) {
        LOG_ERR("IMU init failed");
    }

    /* --- Battery Monitor --- */
    if (!battery.init()) {
        LOG_WRN("Battery monitor init failed — battery readings unavailable");
    }

    /* --- Status LED (scanning state on boot) --- */
    if (!led.init()) {
        LOG_ERR("LED init failed");
    } else {
        led.set_mode(LedManager::Mode::BLINK_FAST);
    }

    /* --- Button --- */
    button.set_double_click_callback(on_button_double_click);
    button.set_long_press_callback(on_button_long_press);
    if (!button.init()) {
        LOG_ERR("Button init failed");
    }

    /* --- BLE --- */
    ble.set_buzzer_callback(on_buzzer_command);
    ble.set_volume_callback(on_volume_command);
    ble.set_alert_config_callback(on_alert_config);
    if (ble.init() != 0) {
        LOG_ERR("BLE init failed");
        return 0;
    }

    /* --- NFC --- */
    if (nfc.init() != 0) {
        LOG_ERR("NFC init failed");
    }

    LOG_INF("HaykulTracker ready");

    /* Main thread is idle — all work is done in Zephyr threads and callbacks. */
    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}
