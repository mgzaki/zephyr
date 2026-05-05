/**
 * @file ble_manager.h
 * @brief BLE peripheral manager for HaykulTracker.
 *
 * Owns the GATT service, advertising, and connection lifecycle.
 *
 * GATT Service: Haykul Service (0000feaa-0000-1000-8000-00805f9b34fb)
 * ─────────────────────────────────────────────────────────────────────
 * | UUID   | Name            | Properties | Notes                       |
 * |--------|-----------------|------------|-----------------------------|
 * | 0xfe01 | Buzzer          | WRITE      | 0x00=off, 0x01=on           |
 * | 0xfe02 | IMU Data        | NOTIFY     | 6-byte raw BMA400 accel     |
 * | 0xfe03 | Buzzer Volume   | WRITE      | 0–100 % (uint8)             |
 * | 0xfe04 | Alert Config    | WRITE      | Disconnect-timeout seconds  |
 * | 0xfe05 | Button Event    | NOTIFY     | 0x01=double-click           |
 * | 0xfe06 | Alert Status    | NOTIFY     | 0x01=proximity alert        |
 *
 * Standard Battery Service (0x180F / 0x2A19) is also registered via
 * Zephyr's BAS module.
 *
 * AlertConfig (1 byte written to 0xfe04)
 * ─────────────────────────────────────
 * Bits [7:0] = seconds before buzzer auto-rings after disconnect (0 = disabled).
 */

#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <zephyr/types.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>

class BleManager {
public:
    /** Invoked when the central writes to the Buzzer characteristic. */
    typedef void (*buzzer_callback_t)(bool enable);

    /** Invoked when the central writes to the Volume characteristic (0–100). */
    typedef void (*volume_callback_t)(uint8_t percent);

    /** Invoked when the central writes an alert-config value (seconds timeout). */
    typedef void (*alert_config_callback_t)(uint8_t seconds);

    BleManager();
    ~BleManager();

    /**
     * @brief Enable Bluetooth and start advertising.
     * @return 0 on success, negative error code on failure.
     */
    int init();

    /** @brief Register application callback for buzzer on/off commands. */
    void set_buzzer_callback(buzzer_callback_t cb);

    /** @brief Register application callback for volume changes (0–100 %). */
    void set_volume_callback(volume_callback_t cb);

    /** @brief Register application callback for alert-config writes. */
    void set_alert_config_callback(alert_config_callback_t cb);

    /**
     * @brief Send a 6-byte IMU acceleration notification to the connected
     *        central.
     * @param data  Pointer to 6 raw bytes from BMA400.
     * @param len   Must be exactly 6.
     * @return 0 on success, -ENOTCONN if no central is connected.
     */
    int notify_imu_data(const uint8_t *data, uint16_t len);

    /**
     * @brief Send a button-event notification to the connected central.
     * @param event  0x01 = double-click (ring phone).
     * @return 0 on success.
     */
    int notify_button_event(uint8_t event);

    /**
     * @brief Send a proximity / alert-status notification.
     * @param status  0x01 = proximity alert triggered.
     * @return 0 on success.
     */
    int notify_alert_status(uint8_t status);

    /**
     * @brief Update the Battery Service level (0–100 %).
     * @param level Battery percentage.
     * @return 0 on success.
     */
    int update_battery_level(uint8_t level);

    /** @brief Return true if a central is currently connected. */
    bool is_connected() const;

    /* ── Zephyr connection callbacks (must be public for BT_CONN_CB_DEFINE) ── */
    static void connected(struct bt_conn *conn, uint8_t err);
    static void disconnected(struct bt_conn *conn, uint8_t reason);

private:
    bool is_initialized;
    buzzer_callback_t       buzzer_cb;
    volume_callback_t       volume_cb;
    alert_config_callback_t alert_config_cb;
};

#endif // BLE_MANAGER_H
