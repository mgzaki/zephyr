/**
 * @file buzzer.h
 * @brief Piezo buzzer driver with volume and frequency control.
 *
 * The buzzer is driven by a BSS138 N-channel MOSFET (Q1) whose gate is
 * connected to P0.14 via a PWM channel.  Volume is controlled by varying
 * the duty cycle:
 *
 *   duty = period × (volume_percent / 200)
 *
 * At 100 % volume the duty cycle is 50 % — the maximum acoustic amplitude
 * for a symmetric square wave on a piezo element.
 *
 * The buzzer is muted when enable(false) is called, but the last set volume
 * is remembered so a subsequent enable(true) restores the previous level.
 */

#ifndef BUZZER_H
#define BUZZER_H

#include <zephyr/drivers/pwm.h>

class Buzzer {
public:
    Buzzer();
    ~Buzzer();

    /**
     * @brief Initialise the PWM channel.
     * @return true on success.
     */
    bool init();

    /**
     * @brief Enable or disable the buzzer at the current volume.
     * @param on true = sound, false = silence.
     */
    void enable(bool on);

    /**
     * @brief Set buzzer volume and immediately apply it if the buzzer is
     *        currently enabled.
     * @param percent  0 (silent) … 100 (maximum loudness).
     */
    void set_volume(uint8_t percent);

    /**
     * @brief Return the current volume setting (0–100).
     */
    uint8_t get_volume() const;

private:
    const struct pwm_dt_spec buzzer_dev;

    /** Remembered volume level; restored by enable(true). */
    uint8_t  volume_pct;

    /** True when the buzzer is currently sounding. */
    bool     is_on;
};

#endif // BUZZER_H
