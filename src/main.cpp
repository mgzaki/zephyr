/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h> /* Gives us access to ARRAY_SIZE macro */

#include "led.h" /* Import our modular LED class */

/* Defines how long each LED stays on before moving to the next */
#define SLEEP_TIME_MS 300

/* The devicetree node identifiers for the LED aliases. */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)

int main(void) {
    /* Instantiate our C++ LED Objects directly from the Hardware Definitions */
    Led leds[] = {
        Led(GPIO_DT_SPEC_GET(LED0_NODE, gpios)),
        Led(GPIO_DT_SPEC_GET(LED1_NODE, gpios)),
        Led(GPIO_DT_SPEC_GET(LED2_NODE, gpios))
    };

    /* Safely get the number of LEDs using Zephyr's native macro */
    const int num_leds = ARRAY_SIZE(leds);

    /* Initialize all LEDs */
    for (int i = 0; i < num_leds; i++) {
        if (!leds[i].init()) {
            printf("Error initializing LED %d\n", i);
            return 0;
        }
        leds[i].off(); // Force default off
    }

    printf("Starting Modular C++ Chaser Object Sequence...\n");

    int active_led_index = 0;

    while (1) {
        /* Step 1: Force all LEDs to turn OFF using object method */
        for (int i = 0; i < num_leds; i++) {
            leds[i].off();
        }

        /* Step 2: Turn ON only the currently active LED object */
        leds[active_led_index].on();

        /* Step 3: Advance to the next LED (wrap around to 0 using modulo math) */
        active_led_index = (active_led_index + 1) % num_leds;

        /* Step 4: Sleep and wait before firing the next sequence */
        k_msleep(SLEEP_TIME_MS);
    }
    
    return 0;
}
