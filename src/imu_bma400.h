#ifndef IMU_BMA400_H
#define IMU_BMA400_H

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

/* BMA400 I2C address (SDO=GND -> 0x14) */
#define BMA400_I2C_ADDR 0x14

/* BMA400 Register Map (key registers) */
#define BMA400_REG_CHIPID       0x00
#define BMA400_REG_STATUS       0x03
#define BMA400_REG_ACC_X_LSB    0x04
#define BMA400_REG_ACC_X_MSB    0x05
#define BMA400_REG_ACC_Y_LSB    0x06
#define BMA400_REG_ACC_Y_MSB    0x07
#define BMA400_REG_ACC_Z_LSB    0x08
#define BMA400_REG_ACC_Z_MSB    0x09
#define BMA400_REG_ACC_CONFIG0  0x19
#define BMA400_REG_ACC_CONFIG1  0x1A
#define BMA400_REG_ACC_CONFIG2  0x1B
#define BMA400_REG_INT_CONFIG0  0x1F
#define BMA400_REG_INT1_MAP     0x21
#define BMA400_REG_CMD          0x7E

#define BMA400_CHIPID_VALUE     0x90

/* Power modes (ACC_CONFIG0 bits [1:0]) */
#define BMA400_POWER_MODE_SLEEP   0x00
#define BMA400_POWER_MODE_LOW     0x01
#define BMA400_POWER_MODE_NORMAL  0x02

class ImuBma400 {
public:
    ImuBma400();
    ~ImuBma400();

    bool init();
    int read_data(uint8_t *buffer, uint16_t max_len);

private:
    const struct device *i2c_dev;
    int read_reg(uint8_t reg, uint8_t *val);
    int write_reg(uint8_t reg, uint8_t val);
};

#endif // IMU_BMA400_H
