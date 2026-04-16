#include "bluetooth.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_class, LOG_LEVEL_INF);

/* Non-connectable, scannable advertising so the device name is visible. */
static const struct bt_le_adv_param adv_param = BT_LE_ADV_PARAM_INIT(
    BT_LE_ADV_OPT_SCANNABLE,
    BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);

Bluetooth::Bluetooth() : is_initialized(false) {}
Bluetooth::~Bluetooth() {}

int Bluetooth::init()
{
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("bt_enable failed (err %d)", err);
    } else {
        is_initialized = true;
    }
    return err;
}

void Bluetooth::update_temperature(int32_t temp_celsius)
{
    if (!is_initialized) return;

    /*
     * WARNING: Do NOT use BT_DATA_BYTES() in C++ files.
     * It relies on C compound literals whose sizeof() is unreliable in C++,
     * causing "Too big advertising data" errors.  Use BT_DATA() with an
     * explicit variable instead (see LEARNING.rst, section 6).
     */
    uint8_t flags    = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
    uint8_t mfg_data[] = { 0xFF, 0xFF, (uint8_t)temp_celsius };

    /*
     * BLE limits each packet to 31 bytes.  We split the data into two parts:
     *
     * ad[] – Advertising data: broadcast with every advertisement.
     *        Contains the discoverability flags and our temperature payload.
     *
     * sd[] – Scan response data: sent only when a scanner (e.g. phone app)
     *        actively requests more details.  Holds the device name so it
     *        shows up as "XIAO TEMP" instead of "Unknown" in nRF Connect.
     *
     * Combining everything into ad[] alone would exceed the 31-byte limit.
     */
    const struct bt_data ad[] = {
        BT_DATA(BT_DATA_FLAGS, &flags, sizeof(flags)),
        BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
    };
    const struct bt_data sd[] = {
        BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
                sizeof(CONFIG_BT_DEVICE_NAME) - 1),
    };

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