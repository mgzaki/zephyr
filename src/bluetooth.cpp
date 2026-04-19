/*
 * bluetooth.cpp — BLE subsystem for the XIAO temperature tracker.
 *
 * This file does three things:
 *   1. Broadcasts the die temperature via BLE advertising packets.
 *   2. Exposes the Immediate Alert Service (IAS, UUID 0x1802) so a
 *      connected phone can trigger an alert on the device.
 *   3. Routes incoming alert writes back to the application through
 *      a registered callback function.
 *
 * Data flow:
 *   Phone writes alert level → Zephyr GATT stack → write_alert_level()
 *     → g_alert_cb() → on_alert_received() in main.cpp
 *       → sets current_alert_level → main loop strobes LEDs
 */

#include "bluetooth.h"
#include <zephyr/bluetooth/bluetooth.h>  /* bt_enable, bt_le_adv_* */
#include <zephyr/bluetooth/uuid.h>       /* BT_UUID_IAS, BT_UUID_ALERT_LEVEL */
#include <zephyr/bluetooth/gatt.h>       /* BT_GATT_SERVICE_DEFINE, etc. */
#include <zephyr/logging/log.h>

/* Register a log module named "bt_class" so log messages from this file
 * appear tagged as [bt_class] in the RTT console output. */
LOG_MODULE_REGISTER(bt_class, LOG_LEVEL_INF);

/* ── Alert callback plumbing ────────────────────────────────────────────
 *
 * Zephyr's GATT layer calls plain C functions, so we can't directly use
 * a C++ member function.  Instead we store a global function pointer
 * (g_alert_cb) that the Bluetooth class sets via set_alert_callback().
 *
 * When a phone writes to the Alert Level characteristic, the Zephyr
 * stack invokes write_alert_level() below, which in turn calls g_alert_cb
 * to notify the application.
 *
 * ALERT FLOW:
 *   Phone app  ──(BLE write)──►  write_alert_level()
 *                                   │
 *                                   ▼
 *                              g_alert_cb(level)      ◄── set by set_alert_callback()
 *                                   │
 *                                   ▼
 *                          on_alert_received()        ◄── defined in main.cpp
 *                                   │
 *                                   ▼
 *                          current_alert_level = level ◄── global in main.cpp
 */
static alert_callback_t g_alert_cb = nullptr;

/* ── GATT write handler ─────────────────────────────────────────────────
 *
 * This is the callback Zephyr invokes whenever a connected BLE central
 * (e.g. a phone) writes to the Alert Level characteristic (UUID 0x2A06).
 *
 * Parameters (all supplied by the Zephyr GATT stack):
 *   conn   – the BLE connection that sent the write
 *   attr   – pointer to the GATT attribute being written
 *   buf    – pointer to the raw bytes the phone sent
 *   len    – number of bytes written (must be exactly 1 for alert level)
 *   offset – byte offset (unused here, always 0 for simple writes)
 *   flags  – write flags (e.g. BT_GATT_WRITE_FLAG_PREPARE)
 *
 * The alert level values per the Bluetooth spec are:
 *   0 = No Alert
 *   1 = Mild Alert
 *   2 = High Alert
 *
 * Returns: number of bytes consumed, or a GATT error code.
 */
static ssize_t write_alert_level(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf,
                                 uint16_t len,
                                 uint16_t offset,
                                 uint8_t flags) {
    /* The Alert Level characteristic is exactly 1 byte.
     * Reject any write that doesn't match. */
    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    /* Extract the single-byte alert level from the write buffer. */
    uint8_t level = *((uint8_t *)buf);
    LOG_INF("Phone sent Alert Level: %d", level);

    /* Forward the alert level to the application via the registered callback.
     * This is where the alert actually reaches your application code —
     * g_alert_cb points to on_alert_received() in main.cpp, which sets
     * the global current_alert_level variable.  The main loop checks that
     * variable every iteration and switches to strobe mode when it's > 0. */
    if (g_alert_cb) {
        g_alert_cb(level);  /* ◄── ALERT IS DELIVERED TO THE APP HERE */
    }

    return len;  /* Tell Zephyr we consumed all bytes successfully. */
}

/* ── GATT service definition ────────────────────────────────────────────
 *
 * BT_GATT_SERVICE_DEFINE is a Zephyr macro that statically registers a
 * GATT service at compile time.  At boot, Zephyr's BLE stack picks it up
 * automatically — no runtime registration needed.
 *
 * Service: Immediate Alert Service (IAS)
 *   UUID: 0x1802  (standard Bluetooth SIG UUID)
 *   Purpose: lets a phone send an alert to the device (e.g. "find my device")
 *
 * Characteristic: Alert Level
 *   UUID: 0x2A06  (standard Bluetooth SIG UUID)
 *   Properties: Write Without Response (fire-and-forget from the phone)
 *   Permissions: writable by any connected central (no encryption required)
 *   Read callback: NULL  (write-only characteristic)
 *   Write callback: write_alert_level  ◄── handles incoming alerts
 *   User data: NULL
 */
BT_GATT_SERVICE_DEFINE(ias_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_IAS),
    BT_GATT_CHARACTERISTIC(BT_UUID_ALERT_LEVEL,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, 
                           write_alert_level, 
                           NULL)
);

/* ── Advertising parameters ─────────────────────────────────────────────
 *
 * These parameters control how the device advertises over BLE:
 *
 *   BT_LE_ADV_OPT_CONN      – the device is connectable, meaning a phone
 *                               can establish a full GATT connection (needed
 *                               for the IAS alert write to work).
 *   BT_LE_ADV_OPT_SCANNABLE – the device responds to scan requests with
 *                               additional data (the scan response contains
 *                               the device name "XIAO_TEMP").
 *
 *   BT_GAP_ADV_FAST_INT_MIN_2 / MAX_2 – fast advertising interval
 *                               (100–150 ms), so nearby phones discover
 *                               the device quickly.
 *
 *   NULL (last param) – no directed advertising; broadcast to everyone.
 */
static const struct bt_le_adv_param adv_param = BT_LE_ADV_PARAM_INIT(
    BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_SCANNABLE,
    BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);

/* ── Bluetooth class implementation ─────────────────────────────────────*/

/* Constructor: just marks the instance as not yet initialized.
 * No BLE operations happen until init() is called. */
Bluetooth::Bluetooth() : is_initialized(false) {}

/* Destructor: nothing to clean up — Zephyr doesn't support
 * tearing down the BLE stack at runtime. */
Bluetooth::~Bluetooth() {}

/*
 * init() — Power on the BLE radio and initialize the Zephyr BLE stack.
 *
 * bt_enable(NULL) is a blocking call that:
 *   1. Initializes the HCI transport to the nRF radio.
 *   2. Loads stored BLE settings (if CONFIG_BT_SETTINGS is enabled).
 *   3. Makes the GATT services (including our IAS) ready to use.
 *
 * The NULL callback means "block until ready" (synchronous init).
 *
 * Returns: 0 on success, negative errno on failure.
 */
int Bluetooth::init() {
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("bt_enable failed (err %d)", err);
    } else {
        is_initialized = true;
    }
    return err;
}

/*
 * set_alert_callback() — Register the function that will be called when
 * the phone writes an alert level.
 *
 * This stores the callback in the global g_alert_cb so that the C-level
 * GATT write handler (write_alert_level) can invoke it.
 *
 * In main.cpp this is called as:
 *   ble.set_alert_callback(on_alert_received);
 *
 * ALERT REGISTRATION HAPPENS HERE — this is where you wire up what
 * happens when the phone triggers a "find my device" alert.
 */
void Bluetooth::set_alert_callback(alert_callback_t cb) {
    g_alert_cb = cb;  /* ◄── ALERT CALLBACK IS REGISTERED HERE */
}

/*
 * update_temperature() — Broadcast the current temperature via BLE advertising.
 *
 * This builds a BLE advertising packet and either starts advertising or
 * updates an already-running advertisement.  Any phone scanning nearby
 * can read the temperature without connecting.
 *
 * Advertising packet structure (ad[]):
 *   ┌──────────────────────────────────────────────────────┐
 *   │ Flags (1 byte):                                      │
 *   │   BT_LE_AD_GENERAL  – device is discoverable         │
 *   │   BT_LE_AD_NO_BREDR – BLE only, no classic Bluetooth │
 *   ├──────────────────────────────────────────────────────┤
 *   │ Manufacturer Data (3 bytes):                         │
 *   │   0xFF, 0xFF – company ID (0xFFFF = "testing/demo")  │
 *   │   temp_celsius – the temperature reading (1 byte)    │
 *   └──────────────────────────────────────────────────────┘
 *
 * Scan response packet (sd[]):
 *   ┌──────────────────────────────────────────────────────┐
 *   │ Complete Local Name: "XIAO_TEMP"                     │
 *   │   (from CONFIG_BT_DEVICE_NAME in prj.conf)          │
 *   └──────────────────────────────────────────────────────┘
 *
 * Note: the temperature is cast to uint8_t, so it wraps at 255°C and
 * cannot represent negative values.  This is fine for die temperature
 * which is typically 20–60°C.
 */
void Bluetooth::update_temperature(int32_t temp_celsius) {
    /* Don't touch BLE if the stack hasn't been initialized yet. */
    if (!is_initialized) return;

    /* AD Flags: tell scanners this is a BLE-only, generally discoverable device. */
    uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;

    /* Manufacturer-specific data: 2-byte company ID + 1-byte temperature.
     * Company ID 0xFFFF is reserved for testing and won't conflict with
     * real manufacturers. */
    uint8_t mfg_data[] = { 0xFF, 0xFF, (uint8_t)temp_celsius };

    /* Build the main advertising data array. */
    const struct bt_data ad[] = {
        BT_DATA(BT_DATA_FLAGS, &flags, sizeof(flags)),
        BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
    };

    /* Build the scan response data (sent when a scanner requests more info). */
    const struct bt_data sd[] = {
        BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
                sizeof(CONFIG_BT_DEVICE_NAME) - 1),
    };

    /* Try to start advertising.  If already advertising (-EALREADY),
     * just update the payload with the new temperature instead. */
    int err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err == -EALREADY) {
        err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    }

    if (err) {
        LOG_ERR("Advertising failed (err %d)", err);
    } else {
        LOG_INF("Temp: %d C", temp_celsius);
    }
}