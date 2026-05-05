/**
 * @file led_manager.h
 * @brief Status LED controller for HaykulTracker.
 *
 * Manages a single GPIO-driven LED (P0.20) used as a software status
 * indicator.  The MCP73831 charger IC already drives D1 (red) directly via
 * its STAT pin — that is pure hardware and requires no firmware involvement.
 *
 * LED modes
 * ---------
 * | Mode           | Pattern                    | Trigger             |
 * |----------------|----------------------------|---------------------|
 * | OFF            | always off                 | normal operation    |
 * | BLINK_SLOW     | 500 ms on / 500 ms off     | battery ≤ 20 %      |
 * | BLINK_FAST     | 100 ms on / 100 ms off     | BLE scanning/pairing|
 * | ON_SOLID       | always on                  | BLE connected       |
 *
 * The LED is active-high (driven through a current-limiting resistor).
 * If the board uses active-low, invert the logic in led_manager.cpp.
 */

#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <zephyr/kernel.h>

class LedManager {
public:
    /** Enumeration of all supported LED display modes. */
    enum class Mode {
        OFF,         /**< LED is permanently off.            */
        BLINK_SLOW,  /**< 1 Hz blink — low battery warning.  */
        BLINK_FAST,  /**< 5 Hz blink — scanning / pairing.   */
        ON_SOLID,    /**< LED permanently on — connected.    */
    };

    LedManager();
    ~LedManager();

    /**
     * @brief Initialise the LED GPIO pin.
     * @return true on success, false if the GPIO device is not ready.
     */
    bool init();

    /**
     * @brief Switch to a new display mode.
     *        Thread-safe — can be called from any context.
     * @param m Desired mode.
     */
    void set_mode(Mode m);

    /**
     * @brief Return the currently active mode.
     */
    Mode get_mode() const;

    /* Internal — called by the blink timer work handler. */
    void toggle();

private:
    Mode current_mode;
    bool led_state;

    k_work_delayable blink_work;

    /**
     * @brief (Re)schedule the next blink toggle based on current_mode.
     *        Must be called from the work-queue context.
     */
    void reschedule_blink();
};

#endif /* LED_MANAGER_H */
