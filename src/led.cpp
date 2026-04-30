#include "led.h"

Led::Led(const struct pwm_dt_spec &led_spec) : spec(led_spec), is_on(false) {}

bool Led::init() {
  if (!pwm_is_ready_dt(&spec)) {
    return false;
  }
  /* Start with LED off */
  pwm_set_pulse_dt(&spec, 0);
  return true;
}

void Led::set_brightness(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  uint32_t pulse = (uint32_t)(spec.period / 100U) * percent;
  pwm_set_pulse_dt(&spec, pulse);
  is_on = (percent > 0);
}

void Led::off() { set_brightness(0); }

void Led::on() { set_brightness(100); }

void Led::toggle() {
  if (is_on) {
    off();
  } else {
    on();
  }
}
