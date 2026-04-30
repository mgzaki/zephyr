#include "imu.h"
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
LOG_MODULE_REGISTER(imu_class, LOG_LEVEL_INF);

Imu::Imu() {
  // The XIAO BLE Sense's LSM6DS3TR-C uses the "st,lsm6dsl" driver in Zephyr
  imu_dev = DEVICE_DT_GET_ANY(st_lsm6dsl);
}

Imu::~Imu() {}

bool Imu::init() {
  if (imu_dev == NULL) {
    LOG_ERR("Could not find IMU in DeviceTree!");
    return false;
  }

  if (!device_is_ready(imu_dev)) {
    LOG_ERR("IMU is on the bus, but not responding. Check I2C lines.");
    return false;
  }

  LOG_INF("6-Axis IMU successfully initialized on I2C bus!");
  return true;
}

void Imu::read_acceleration(double &x, double &y, double &z) {
  if (!imu_dev)
    return;

  struct sensor_value accel_x, accel_y, accel_z;

  // 1. Fetch all sensor channels at once (the LSM6DSL driver expects a full
  // fetch)
  int err = sensor_sample_fetch(imu_dev);
  if (err) {
    LOG_ERR("IMU fetch failed (err %d)", err);
    return;
  }

  // 2. Pull the X, Y, and Z data across the I2C wires into our memory
  sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_X, &accel_x);
  sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Y, &accel_y);
  sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Z, &accel_z);

  // 3. Convert Zephyr's two-part integer system into a clean C++ double (m/s²)
  x = sensor_value_to_double(&accel_x);
  y = sensor_value_to_double(&accel_y);
  z = sensor_value_to_double(&accel_z);
}

// Power Management Methods

void Imu::wake() {
  if (!imu_dev)
    return;
  int err = pm_device_action_run(imu_dev, PM_DEVICE_ACTION_RESUME);
  if (err) {
    LOG_WRN("Failed to wake IMU (err %d)", err);
  }
}

void Imu::sleep() {
  if (!imu_dev)
    return;
  int err = pm_device_action_run(imu_dev, PM_DEVICE_ACTION_SUSPEND);
  if (err) {
    LOG_WRN("Failed to suspend IMU (err %d)", err);
  }
}
