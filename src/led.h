#ifndef LED_H
#define LED_H

#include <zephyr/drivers/gpio.h>

/**
 * Encapsulate LED hardware control into a tidy C++ Class
 */
class Led {
private:
    struct gpio_dt_spec spec;

public:
    /* Constructor: initializes the spec from Devicetree */
    Led(const struct gpio_dt_spec& led_spec);

    /* Setup the hardware pin */
    bool init();

    /* Base control method */
    void set_state(bool is_on);

    /* Helper wrapper for OFF */
    void off();
    
    /* Helper wrapper for ON */
    void on();
    
    /* Helper wrapper for TOGGLE */
    void toggle();
};

#endif // LED_H
