/**
 * @file battery_monitor.cpp
 * @brief Battery level measurement via nRF52840 internal SAADC.
 *
 * The SAADC is configured to sample the internal VDDHDIV5 channel:
 *   - Gain         : 1/6
 *   - Reference    : Internal (0.6 V)
 *   - Acquisition  : 40 µs
 *   - Resolution   : 12-bit (4096 counts)
 *
 * With gain 1/6 and reference 0.6 V the full-scale input is 3.6 V.
 * VDDHDIV5 = VDD / 5, so max detectable VDD ≈ 18 V — way above Li-Po
 * maximum, giving us plenty of headroom.
 *
 * Conversion formula:
 *   mV_VDDHDIV5 = (raw / 4096) * 3600  (since full-scale = 3.6 V = 3600 mV)
 *   mV_VDD      = mV_VDDHDIV5 * 5
 */

#include "battery_monitor.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery_monitor, LOG_LEVEL_INF);

/* ── ADC configuration ───────────────────────────────────────────────────── */
#if DT_NODE_EXISTS(DT_NODELABEL(adc))
static const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
#else
static const struct device *adc_dev = nullptr;
#endif

/** SAADC channel ID reserved for VDD measurement. */
static constexpr uint8_t ADC_VDD_CHANNEL = 0;

static const struct adc_channel_cfg vdd_ch_cfg = {
    .gain             = ADC_GAIN_1_6,
    .reference        = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
    .channel_id       = ADC_VDD_CHANNEL,
    .input_positive   = SAADC_CH_PSELP_PSELP_VDD, /* Internal VDD */
};

/* ── Li-Po discharge table ───────────────────────────────────────────────── */
struct {
    int32_t mv;   /* VDD millivolts */
    uint8_t pct;  /* Estimated % */
} static constexpr lipo_table[] = {
    { 4200, 100 },
    { 4000,  90 },
    { 3900,  80 },
    { 3800,  70 },
    { 3750,  60 },
    { 3700,  50 },
    { 3650,  40 },
    { 3600,  30 },
    { 3500,  20 },
    { 3400,  10 },
    { 3300,   0 },
};
static constexpr size_t LIPO_TABLE_LEN = sizeof(lipo_table) / sizeof(lipo_table[0]);

/* ── Constructor / Destructor ────────────────────────────────────────────── */

BatteryMonitor::BatteryMonitor() : initialised(false) {}
BatteryMonitor::~BatteryMonitor() {}

/* ── Public API ──────────────────────────────────────────────────────────── */

bool BatteryMonitor::init()
{
    if (!adc_dev || !device_is_ready(adc_dev)) {
        LOG_WRN("ADC device not ready — battery level will be estimated as 100%%");
        return false;
    }

    int err = adc_channel_setup(adc_dev, &vdd_ch_cfg);
    if (err) {
        LOG_ERR("ADC channel setup failed (err %d)", err);
        return false;
    }

    initialised = true;
    LOG_INF("Battery monitor initialised (VDDHDIV5 channel)");
    return true;
}

uint8_t BatteryMonitor::read_level_percent()
{
    if (!initialised) {
        /* No ADC — return a safe default so BAS still reports something. */
        return 100U;
    }

    int16_t raw = 0;
    struct adc_sequence seq = {
        .channels    = BIT(ADC_VDD_CHANNEL),
        .buffer      = &raw,
        .buffer_size = sizeof(raw),
        .resolution  = 12,
        .oversampling = 4,
    };

    int err = adc_read(adc_dev, &seq);
    if (err) {
        LOG_ERR("ADC read failed (err %d)", err);
        return 0U;
    }

    /* raw is in range [0, 4095] for 12-bit.
     * VDDHDIV5 full-scale corresponds to 3600 mV (internal ref 0.6 V × gain 6). */
    int32_t mv_div5 = (int32_t)raw * 3600 / 4096;
    int32_t mv_vdd  = mv_div5 * 5;

    LOG_DBG("VDD raw=%d, mV=%d", raw, mv_vdd);
    return mv_to_percent(mv_vdd);
}

bool BatteryMonitor::is_low()
{
    return read_level_percent() <= BATTERY_LOW_THRESHOLD_PCT;
}

/* ── Private helpers ─────────────────────────────────────────────────────── */

uint8_t BatteryMonitor::mv_to_percent(int32_t mv)
{
    /* Above max → 100 % */
    if (mv >= lipo_table[0].mv) return 100U;
    /* Below min → 0 % */
    if (mv <= lipo_table[LIPO_TABLE_LEN - 1].mv) return 0U;

    /* Linear interpolation between adjacent table entries */
    for (size_t i = 0; i < LIPO_TABLE_LEN - 1; i++) {
        if (mv <= lipo_table[i].mv && mv > lipo_table[i + 1].mv) {
            int32_t range_mv  = lipo_table[i].mv  - lipo_table[i + 1].mv;
            int32_t range_pct = lipo_table[i].pct - lipo_table[i + 1].pct;
            int32_t offset_mv = mv - lipo_table[i + 1].mv;
            return (uint8_t)(lipo_table[i + 1].pct +
                             (offset_mv * range_pct / range_mv));
        }
    }
    return 0U;
}
