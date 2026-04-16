/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h> /* Gives us access to ARRAY_SIZE macro */

#include "led.h" /* Import our modular LED class */

/* Step size and delay used for the fade sweep */
#define FADE_STEP_PERCENT  5
#define FADE_STEP_MS       20

/* The devicetree node identifiers for the PWM LED aliases. */
#define LED0_NODE DT_ALIAS(pwm_led0)
#define LED1_NODE DT_ALIAS(pwm_led1)
#define LED2_NODE DT_ALIAS(pwm_led2)

int main(void) {
    /* Instantiate our C++ LED Objects from the PWM Devicetree specs */
    Led leds[] = {
        Led(PWM_DT_SPEC_GET(LED0_NODE)),
        Led(PWM_DT_SPEC_GET(LED1_NODE)),
        Led(PWM_DT_SPEC_GET(LED2_NODE))
    };

    const int num_leds = ARRAY_SIZE(leds);

    /* Initialize all LEDs */
    for (int i = 0; i < num_leds; i++) {
        if (!leds[i].init()) {
            printf("Error initializing LED %d\n", i);
            return 0;
        }
    }

    printf("Starting PWM LED dimming chaser...\n");

    int active = 0;

    while (1) {
        /* Fade the active LED in from 0 % to 100 % */
        for (int b = 0; b <= 100; b += FADE_STEP_PERCENT) {
            leds[active].set_brightness(b);
            k_msleep(FADE_STEP_MS);
        }

        /* Fade it back out to 0 % */
        for (int b = 100; b >= 0; b -= FADE_STEP_PERCENT) {
            leds[active].set_brightness(b);
            k_msleep(FADE_STEP_MS);
        }

        /* Advance to the next LED */
        active = (active + 1) % num_leds;
    }

    return 0;
}

