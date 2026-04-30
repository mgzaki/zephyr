#ifndef IMU_H
#define IMU_H

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

class Imu {
public:
  Imu();
  ~Imu();

  // Wakes up the I2C bus and finds the sensor
  bool init();

  // Pulls the latest accelerometer data
  void read_acceleration(double &x, double &y, double &z);

  // Power Management Methods
  void wake();
  void sleep();

private:
  const struct device *imu_dev;
};

#endif // IMU_H