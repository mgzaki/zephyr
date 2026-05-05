/**
 * @file buzzer.cpp
 * @brief Piezo buzzer implementation.
 */

#include "buzzer.h"
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(buzzer, LOG_LEVEL_INF);

/**
 * @brief Construct a Buzzer, obtaining the PWM spec from the devicetree
 *        "buzzer-pwm" alias defined in the board overlay.
 */
Buzzer::Buzzer()
    : buzzer_dev(PWM_DT_SPEC_GET(DT_ALIAS(buzzer_pwm))),
      volume_pct(100U),
      is_on(false)
{}

Buzzer::~Buzzer() {}

bool Buzzer::init()
{
    if (!pwm_is_ready_dt(&buzzer_dev)) {
        LOG_ERR("PWM buzzer device not ready");
        return false;
    }
    /* Ensure buzzer starts silenced */
    pwm_set_dt(&buzzer_dev, buzzer_dev.period, 0);
    LOG_INF("Buzzer initialised (default volume %d%%)", volume_pct);
    return true;
}

/**
 * @brief Enable or silence the buzzer without changing the stored volume.
 *
 * When on=true the PWM duty cycle is computed from volume_pct so that
 * 100 % volume corresponds to a 50 % duty cycle (maximum square-wave
 * amplitude for a piezo element).  The formula is:
 *
 *   duty_ns = period_ns × volume_pct / 200
 *
 * Example: period = 250 000 ns (4 kHz), volume 80 % → duty = 100 000 ns.
 */
void Buzzer::enable(bool on)
{
    is_on = on;
    if (on) {
        uint32_t duty = (uint64_t)buzzer_dev.period * volume_pct / 200U;
        pwm_set_dt(&buzzer_dev, buzzer_dev.period, duty);
        LOG_DBG("Buzzer ON (volume=%d%%, duty=%u ns)", volume_pct, duty);
    } else {
        pwm_set_dt(&buzzer_dev, buzzer_dev.period, 0);
        LOG_DBG("Buzzer OFF");
    }
}

/**
 * @brief Set the volume and immediately apply it if the buzzer is active.
 * @param percent  0 = silent, 100 = loudest (50 % duty cycle).
 */
void Buzzer::set_volume(uint8_t percent)
{
    if (percent > 100U) percent = 100U;
    volume_pct = percent;
    LOG_INF("Buzzer volume set to %d%%", volume_pct);
    if (is_on) {
        /* Reapply with the new volume immediately */
        enable(true);
    }
}

uint8_t Buzzer::get_volume() const
{
    return volume_pct;
}
