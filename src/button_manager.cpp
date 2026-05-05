/**
 * @file button_manager.cpp
 * @brief Push-button input handler implementation.
 *
 * Hardware
 * --------
 * - GPIO : P1.10  (active-low, internal pull-up)
 * - Connected between P1.10 and GND (SW1 on haykul_tracker schematic).
 *
 * Gesture state machine
 * ---------------------
 *  IDLE ──[press]──► PRESS_PENDING
 *         debounce timer fires, pin stable-low
 *             ├─[release within LONG_PRESS_MS]──► COUNT_PRESS
 *             │       press_count++
 *             │       start double_click_work (DOUBLE_CLICK_WINDOW_MS)
 *             │           ├─[second press arrives before timer]──►
 *             │           │       press_count == 2 → DOUBLE_CLICK callback
 *             │           └─[timer fires, press_count == 1]──► IDLE (single click, ignored)
 *             └─[still held after LONG_PRESS_MS]──► LONG_PRESS callback → IDLE
 */

#include "button_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button_manager, LOG_LEVEL_INF);

/* ── Devicetree alias ─────────────────────────────────────────────────────── */
#define BTN_NODE DT_ALIAS(user_button)

/* ── Static context pointer for C callbacks ──────────────────────────────── */
static ButtonManager *g_instance = nullptr;

/* ── k_work_delayable wrappers ───────────────────────────────────────────── */

/**
 * @brief Work handler: fired DEBOUNCE_MS after a GPIO edge.
 *        Reads the stable pin state and forwards to the instance.
 */
static void debounce_work_fn(k_work *w)
{
    if (!g_instance) return;
    k_work_delayable *dw = CONTAINER_OF(w, k_work_delayable, work);
    (void)dw;

    /* Re-read pin to confirm stable state */
    const gpio_dt_spec *spec = &g_instance->btn_spec;   /* accessed via friend-like pattern */
    /* ButtonManager exposes the spec as public for this accessor */
    bool pressed = (gpio_pin_get_dt(spec) == 0); /* active-low */
    g_instance->handle_debounced_press(pressed);
}

/**
 * @brief Work handler: fired LONG_PRESS_MS after button was pressed.
 *        If button is still held, triggers the long-press callback.
 */
static void long_press_work_fn(k_work *w)
{
    if (g_instance) g_instance->handle_long_press_timeout();
}

/**
 * @brief Work handler: fired DOUBLE_CLICK_WINDOW_MS after the first press.
 *        Evaluates the press count accumulated in the window.
 */
static void double_click_work_fn(k_work *w)
{
    if (g_instance) g_instance->handle_double_click_timeout();
}

/* ── GPIO interrupt callback ─────────────────────────────────────────────── */

/**
 * @brief Raw GPIO ISR: schedules the debounce work item.
 *        Runs in interrupt context — must be minimal.
 */
static void gpio_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    (void)dev; (void)pins;
    if (!g_instance) return;
    /* Cancel any pending debounce and reschedule from now */
    k_work_reschedule(&g_instance->debounce_work,
                      K_MSEC(ButtonManager::DEBOUNCE_MS));
}

/* ── Constructor / Destructor ────────────────────────────────────────────── */

ButtonManager::ButtonManager()
    : btn_spec(GPIO_DT_SPEC_GET(BTN_NODE, gpios)),
      dc_cb(nullptr), lp_cb(nullptr),
      press_count(0), button_held(false)
{
}

ButtonManager::~ButtonManager()
{
    g_instance = nullptr;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

bool ButtonManager::init()
{
    if (!gpio_is_ready_dt(&btn_spec)) {
        LOG_ERR("Button GPIO device not ready");
        return false;
    }

    int err = gpio_pin_configure_dt(&btn_spec, GPIO_INPUT | GPIO_PULL_UP);
    if (err) {
        LOG_ERR("Button GPIO configure failed (err %d)", err);
        return false;
    }

    err = gpio_pin_interrupt_configure_dt(&btn_spec, GPIO_INT_EDGE_BOTH);
    if (err) {
        LOG_ERR("Button interrupt configure failed (err %d)", err);
        return false;
    }

    gpio_init_callback(&btn_cb_data, gpio_isr, BIT(btn_spec.pin));
    gpio_add_callback(btn_spec.port, &btn_cb_data);

    /* Initialise work items */
    k_work_init_delayable(&debounce_work,      debounce_work_fn);
    k_work_init_delayable(&long_press_work,    long_press_work_fn);
    k_work_init_delayable(&double_click_work,  double_click_work_fn);

    g_instance = this;

    LOG_INF("Button initialised on P1.10 (double-click=ring phone, long-press=silence buzzer)");
    return true;
}

void ButtonManager::set_callbacks(double_click_cb_t dc, long_press_cb_t lp)
{
    dc_cb = dc;
    lp_cb = lp;
}

/* ── Internal state-machine helpers ─────────────────────────────────────── */

/**
 * @brief Called from debounce_work after the pin has been stable for DEBOUNCE_MS.
 * @param pressed true if the button is currently pressed (pin low).
 */
void ButtonManager::handle_debounced_press(bool pressed)
{
    if (pressed && !button_held) {
        /* Rising-edge: button just pressed */
        button_held = true;
        LOG_DBG("Button pressed");

        /* Start long-press timer */
        k_work_reschedule(&long_press_work, K_MSEC(LONG_PRESS_MS));

        /* Count this press in the double-click window */
        press_count++;

        if (press_count == 1) {
            /* First press: open a window to catch a second press */
            k_work_reschedule(&double_click_work, K_MSEC(DOUBLE_CLICK_WINDOW_MS));
        } else if (press_count >= 2) {
            /* Second (or more) press inside the window → double-click */
            k_work_cancel_delayable(&double_click_work);
            press_count = 0;
            LOG_INF("Double-click detected → ring phone");
            if (dc_cb) dc_cb();
        }
    } else if (!pressed && button_held) {
        /* Falling-edge: button released */
        button_held = false;
        LOG_DBG("Button released");

        /* Cancel long-press timer (button released before threshold) */
        k_work_cancel_delayable(&long_press_work);
    }
}

/**
 * @brief Called when the long-press timer fires.
 *        If the button is still held, this is a confirmed long-press.
 */
void ButtonManager::handle_long_press_timeout()
{
    if (button_held) {
        /* Cancel any pending double-click window — long-press takes priority */
        k_work_cancel_delayable(&double_click_work);
        press_count = 0;
        LOG_INF("Long-press detected → silencing buzzer");
        if (lp_cb) lp_cb();
    }
}

/**
 * @brief Called when the double-click window timer fires.
 *        If press_count == 1 after the window, it was a single click (ignored).
 */
void ButtonManager::handle_double_click_timeout()
{
    if (press_count == 1) {
        LOG_DBG("Single click — ignored");
    }
    press_count = 0;
}
