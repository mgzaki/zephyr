#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stdint.h>

/* Define a callback type for when the phone triggers an alert */
typedef void (*alert_callback_t)(uint8_t level);

class Bluetooth {
public:
    Bluetooth();
    ~Bluetooth();

    // Wakes up the Bluetooth radio
    int init();

    // Updates the broadcast packet with the new temperature
    void update_temperature(int32_t temp_celsius);

    // Let the main application register its "Ring" function
    void set_alert_callback(alert_callback_t cb);

private:
    bool is_initialized;
};

#endif // BLUETOOTH_H