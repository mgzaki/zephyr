/**
 * @file led_manager.cpp
 * @brief Status LED implementation.
 *
 * Hardware
 * --------
 * - GPIO : P0.20, active-high, driven via a 330 Ω current-limiting resistor.
 *   Add the `status-led` alias in the board overlay:
 *     aliases { status-led = &status_led_0; };
 *
 * The MCP73831 STAT pin → D1 circuit is independent hardware and is NOT
 * managed here.  It lights the red LED when the battery is charging without
 * any firmware interaction.
 */

#include "led_manager.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_manager, LOG_LEVEL_INF);

/* ── Devicetree alias ─────────────────────────────────────────────────────── */
#define LED_NODE DT_ALIAS(status_led)

static const gpio_dt_spec led_spec = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* ── Static instance for work-handler callback ───────────────────────────── */
static LedManager *g_led_instance = nullptr;

static void blink_work_fn(k_work *w)
{
    if (g_led_instance) g_led_instance->toggle();
}

/* ── Constructor / Destructor ────────────────────────────────────────────── */

LedManager::LedManager()
    : current_mode(Mode::OFF), led_state(false)
{
}

LedManager::~LedManager()
{
    g_led_instance = nullptr;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

bool LedManager::init()
{
    if (!gpio_is_ready_dt(&led_spec)) {
        LOG_ERR("Status LED GPIO device not ready");
        return false;
    }

    int err = gpio_pin_configure_dt(&led_spec, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("Status LED GPIO configure failed (err %d)", err);
        return false;
    }

    k_work_init_delayable(&blink_work, blink_work_fn);
    g_led_instance = this;

    LOG_INF("Status LED initialised (P0.20)");
    return true;
}

void LedManager::set_mode(Mode m)
{
    current_mode = m;
    /* Cancel any pending blink tick so we start fresh */
    k_work_cancel_delayable(&blink_work);
    led_state = false;

    switch (m) {
    case Mode::OFF:
        gpio_pin_set_dt(&led_spec, 0);
        break;

    case Mode::ON_SOLID:
        gpio_pin_set_dt(&led_spec, 1);
        break;

    case Mode::BLINK_SLOW:
    case Mode::BLINK_FAST:
        /* Immediately start the first tick */
        k_work_schedule(&blink_work, K_NO_WAIT);
        break;
    }
}

LedManager::Mode LedManager::get_mode() const
{
    return current_mode;
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

/**
 * @brief Toggle LED state and reschedule the next toggle based on mode.
 *        Called from the system work-queue via blink_work.
 */
void LedManager::toggle()
{
    if (current_mode != Mode::BLINK_SLOW && current_mode != Mode::BLINK_FAST) {
        return; /* Mode changed while work was pending — do nothing */
    }

    led_state = !led_state;
    gpio_pin_set_dt(&led_spec, led_state ? 1 : 0);
    reschedule_blink();
}

void LedManager::reschedule_blink()
{
    k_timeout_t period;
    switch (current_mode) {
    case Mode::BLINK_SLOW:
        period = K_MSEC(500); /* 1 Hz: 500 ms per half-period */
        break;
    case Mode::BLINK_FAST:
        period = K_MSEC(100); /* 5 Hz: 100 ms per half-period */
        break;
    default:
        return;
    }
    k_work_schedule(&blink_work, period);
}
