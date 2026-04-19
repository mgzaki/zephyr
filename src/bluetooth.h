#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stdint.h>

/* Define a callback type for when the phone triggers an alert */
typedef void (*alert_callback_t)(uint8_t level);

/*
 * bt_get_brightness() — Read the current LED brightness set by the phone.
 *
 * This is a free function (not a class method) because the brightness
 * value lives in a file-scope static variable (g_brightness_level) inside
 * bluetooth.cpp.  The BLE write handler updates that variable whenever
 * the phone writes to the custom Brightness characteristic.
 *
 * The main loop calls this every iteration to decide how bright to
 * drive the LEDs:
 *   - 0   → use the default idle fade animation
 *   - 1–100 → set all LEDs to this fixed brightness percentage
 *
 * Returns: brightness percentage (0–100).
 */
uint8_t bt_get_brightness(void);

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