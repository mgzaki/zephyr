#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stdint.h>

class Bluetooth {
public:
    Bluetooth();
    ~Bluetooth();

    // Wakes up the Bluetooth radio
    int init();

    // Updates the broadcast packet with the new temperature
    void update_temperature(int32_t temp_celsius);

private:
    bool is_initialized;
};

#endif // BLUETOOTH_H