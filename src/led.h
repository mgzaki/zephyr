#ifndef LED_H
#define LED_H

#include <zephyr/drivers/pwm.h>

/**
 * Encapsulate LED hardware control into a tidy C++ Class.
 * Uses PWM so brightness can be varied with set_brightness().
 */
class Led {
private:
    struct pwm_dt_spec spec;

public:
    /* Constructor: initializes the spec from Devicetree */
    Led(const struct pwm_dt_spec& led_spec);

    /* Setup the PWM channel */
    bool init();

    /* Set brightness 0 (off) … 100 (full on) */
    void set_brightness(uint8_t percent);

    /* Helper wrapper for OFF */
    void off();

    /* Helper wrapper for ON (full brightness) */
    void on();

    /* Helper wrapper for TOGGLE (full brightness <-> off) */
    void toggle();

private:
    bool is_on;
};

#endif // LED_H
