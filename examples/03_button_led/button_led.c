/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * button_led.c - TikuOS Example 03: Button-controlled LED
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file    button_led.c
 * @brief   TikuOS Example 03 - Button-Controlled LED
 *
 * Polls the button state periodically. When button S1 is pressed,
 * LED1 toggles.  Demonstrates polling-based input handling and
 * edge detection.
 *
 * Hardware: MSP430FR5969 LaunchPad
 *   Button S1 = P4.5 (active low, internal pull-up)
 *   LED1      = P4.6
 *
 * What you learn:
 *   - Reading button state via TIKU_BOARD_BTN1_PRESSED()
 *   - Polling at a fixed rate for debouncing
 *   - Simple edge detection with a static variable
 */

#include "tiku.h"

#if TIKU_EXAMPLE_BUTTON_LED

/*--------------------------------------------------------------------------*/
/* Process declaration                                                       */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(button_led_process, "Button LED");

/*--------------------------------------------------------------------------*/
/* Process thread                                                            */
/*--------------------------------------------------------------------------*/
static struct tiku_timer poll_timer;

TIKU_PROCESS_THREAD(button_led_process, ev, data)
{
    static uint8_t prev_pressed = 0;

    TIKU_PROCESS_BEGIN();

    tiku_common_led1_init();
    TIKU_BOARD_BTN1_INIT();

    /* Poll every 50 ms (20 Hz) -- good for debouncing */
    tiku_timer_set_event(&poll_timer, TIKU_CLOCK_MS_TO_TICKS(50));

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        uint8_t pressed = TIKU_BOARD_BTN1_PRESSED();

        /* Rising edge: was released, now pressed */
        if (pressed && !prev_pressed) {
            tiku_common_led1_toggle();
        }
        prev_pressed = pressed;

        tiku_timer_reset(&poll_timer);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart                                                                 */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&button_led_process);

#endif /* TIKU_EXAMPLE_BUTTON_LED */
