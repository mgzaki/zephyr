/**
 * @file battery_monitor.h
 * @brief Battery voltage measurement and level estimation.
 *
 * Uses the nRF52840 internal SAADC to measure the supply voltage via the
 * VDDHDIV5 input channel (VDD ÷ 5).  No external voltage divider is required.
 *
 * The measured VDD is compared against a Li-Po discharge curve to produce a
 * 0–100 % state-of-charge estimate.  The estimate is intentionally coarse
 * (±5 %) because the SAADC measures the regulated output, not the raw cell
 * voltage, but it is accurate enough for low-battery warnings.
 *
 * Typical Li-Po VDD thresholds (after regulator losses):
 *   100 % → 4.20 V
 *    80 % → 3.90 V
 *    60 % → 3.75 V
 *    40 % → 3.65 V
 *    20 % → 3.50 V
 *     0 % → 3.30 V  (under-voltage cut-off)
 *
 * Usage
 * -----
 * @code
 *   BatteryMonitor bat;
 *   bat.init();
 *   uint8_t pct = bat.read_level_percent();
 * @endcode
 */

#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <zephyr/types.h>

/** Battery level below which the low-battery LED starts blinking. */
constexpr uint8_t BATTERY_LOW_THRESHOLD_PCT = 20U;

class BatteryMonitor {
public:
    BatteryMonitor();
    ~BatteryMonitor();

    /**
     * @brief Prepare the SAADC channel for VDD measurement.
     * @return true on success.
     */
    bool init();

    /**
     * @brief Perform one ADC conversion and return an estimated percentage.
     * @return Battery level 0–100 %, or 0 if measurement failed.
     */
    uint8_t read_level_percent();

    /**
     * @brief Convenience: return true when level is at or below the low
     *        threshold defined by BATTERY_LOW_THRESHOLD_PCT.
     */
    bool is_low();

private:
    bool initialised;

    /**
     * @brief Map a raw millivolt VDD reading to 0–100 %.
     * @param mv Measured VDD in millivolts.
     * @return Estimated state-of-charge percentage.
     */
    static uint8_t mv_to_percent(int32_t mv);
};

#endif /* BATTERY_MONITOR_H */
