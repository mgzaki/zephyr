#include "imu_bma400.h"
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(imu_bma400, LOG_LEVEL_INF);

/*
 * BMA400 Accelerometer Driver (raw I2C)
 *
 * Hardware connections (from schematic):
 *   SCL -> P0.12 (I2C0)
 *   SDA -> P0.11 (I2C0)
 *   CSB -> VDD   (I2C mode selected)
 *   SDO -> GND   (I2C address = 0x14)
 *   INT1 -> P0.13
 */
ImuBma400::ImuBma400() : i2c_dev(DEVICE_DT_GET(DT_NODELABEL(i2c0))) {}

ImuBma400::~ImuBma400() {}

int ImuBma400::read_reg(uint8_t reg, uint8_t *val) {
    return i2c_reg_read_byte(i2c_dev, BMA400_I2C_ADDR, reg, val);
}

int ImuBma400::write_reg(uint8_t reg, uint8_t val) {
    return i2c_reg_write_byte(i2c_dev, BMA400_I2C_ADDR, reg, val);
}

bool ImuBma400::init() {
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C0 bus not ready");
        return false;
    }

    /* Verify chip ID */
    uint8_t chip_id = 0;
    int err = read_reg(BMA400_REG_CHIPID, &chip_id);
    if (err) {
        LOG_ERR("Failed to read BMA400 chip ID (err %d)", err);
        return false;
    }
    if (chip_id != BMA400_CHIPID_VALUE) {
        LOG_ERR("Unexpected chip ID: 0x%02X (expected 0x%02X)", chip_id, BMA400_CHIPID_VALUE);
        return false;
    }

    /* Set Normal power mode */
    err = write_reg(BMA400_REG_ACC_CONFIG0, BMA400_POWER_MODE_NORMAL);
    if (err) {
        LOG_ERR("Failed to set power mode (err %d)", err);
        return false;
    }

    /* Wait for mode transition */
    k_msleep(5);

    /* Configure: ±4g range, 100Hz ODR */
    err = write_reg(BMA400_REG_ACC_CONFIG1, 0x49);  /* ODR=100Hz, OSR=1, range=±4g */
    if (err) {
        LOG_ERR("Failed to configure ACC_CONFIG1 (err %d)", err);
        return false;
    }

    LOG_INF("BMA400 initialized (chip ID: 0x%02X, I2C addr: 0x%02X)", chip_id, BMA400_I2C_ADDR);
    return true;
}

int ImuBma400::read_data(uint8_t *buffer, uint16_t max_len) {
    if (!device_is_ready(i2c_dev)) return -ENODEV;
    if (max_len < 6) return -ENOMEM;

    /* Burst-read 6 bytes starting from ACC_X_LSB (0x04) */
    uint8_t raw[6];
    int err = i2c_burst_read(i2c_dev, BMA400_I2C_ADDR, BMA400_REG_ACC_X_LSB, raw, sizeof(raw));
    if (err) return err;

    /* BMA400 provides 12-bit signed values in LSB/MSB pairs.
     * The raw data is: [X_LSB, X_MSB, Y_LSB, Y_MSB, Z_LSB, Z_MSB]
     * MSB contains bits [11:4], LSB contains bits [3:0] in upper nibble.
     * For BLE notification, we forward the raw 6 bytes directly. */
    memcpy(buffer, raw, 6);

    return 6;
}
