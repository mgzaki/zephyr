/**
 * @file button_manager.h
 * @brief Push-button input handler for HaykulTracker.
 *
 * Manages the single push-button (P1.10) connected between the GPIO pin and
 * GND, with Zephyr's internal pull-up enabled.
 *
 * Supported gestures:
 *   - DOUBLE_CLICK : two presses within DOUBLE_CLICK_WINDOW_MS (500 ms)
 *                    → notifies paired phone to ring.
 *   - LONG_PRESS   : button held for LONG_PRESS_MS (3 000 ms)
 *                    → silences the local buzzer.
 *
 * Debounce is performed entirely in software: the interrupt handler schedules
 * a k_work_delayable that fires DEBOUNCE_MS (20 ms) after the edge, then
 * checks the stable GPIO state.
 *
 * Usage
 * -----
 * 1. Implement the two callback typedefs.
 * 2. Call ButtonManager::init() once in main().
 * 3. Callbacks are invoked from the system work-queue context — they must not
 *    block for long periods.
 */

#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <zephyr/drivers/gpio.h>

class ButtonManager {
public:
    /** Called when a double-click gesture is detected. */
    typedef void (*double_click_cb_t)(void);

    /** Called when a long-press gesture (≥ 3 s) is detected. */
    typedef void (*long_press_cb_t)(void);

    ButtonManager();
    ~ButtonManager();

    /**
     * @brief Initialise the button GPIO and register interrupt.
     * @return true on success, false if GPIO device is not ready.
     */
    bool init();

    /**
     * @brief Register application callbacks for button gestures.
     * @param dc_cb  Invoked on double-click.
     * @param lp_cb  Invoked on long-press.
     */
    void set_callbacks(double_click_cb_t dc_cb, long_press_cb_t lp_cb);

    /**
     * @brief Register the double-click callback individually.
     * @param cb  Invoked when a double-click gesture is detected.
     */
    void set_double_click_callback(double_click_cb_t cb);

    /**
     * @brief Register the long-press callback individually.
     * @param cb  Invoked when the button is held for LONG_PRESS_MS.
     */
    void set_long_press_callback(long_press_cb_t cb);

    /* ── Internal helpers called from static Zephyr work handlers ── */
    void handle_debounced_press(bool pressed);
    void handle_long_press_timeout();
    void handle_double_click_timeout();

    /* ── Members accessed by static C callbacks (must be public) ── */
    /** Debounce delay in milliseconds. */
    static constexpr int DEBOUNCE_MS           = 20;
    /** Window within which two presses count as a double-click (ms). */
    static constexpr int DOUBLE_CLICK_WINDOW_MS = 500;
    /** Minimum hold duration to trigger long-press (ms). */
    static constexpr int LONG_PRESS_MS          = 3000;

    gpio_dt_spec     btn_spec;
    k_work_delayable debounce_work;

private:
    gpio_callback btn_cb_data;

    double_click_cb_t dc_cb;
    long_press_cb_t   lp_cb;

    /** Number of presses recorded in the current double-click window. */
    int press_count;
    /** True while button is physically held down. */
    bool button_held;

    k_work_delayable long_press_work;
    k_work_delayable double_click_work;
};

#endif /* BUTTON_MANAGER_H */
