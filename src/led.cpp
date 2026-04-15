#include "led.h"

Led::Led(const struct gpio_dt_spec& led_spec) : spec(led_spec) {}

bool Led::init() {
    if (!gpio_is_ready_dt(&spec)) {
        return false;
    }
    if (gpio_pin_configure_dt(&spec, GPIO_OUTPUT_ACTIVE) < 0) {
        return false;
    }
    return true;
}

void Led::set_state(bool is_on) {
    gpio_pin_set_dt(&spec, is_on ? 1 : 0);
}

void Led::off() {
    set_state(false);
}

void Led::on() {
    set_state(true);
}

void Led::toggle() {
    gpio_pin_toggle_dt(&spec);
}
