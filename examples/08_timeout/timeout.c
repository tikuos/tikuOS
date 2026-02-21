/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * timeout.c - TikuOS Example 08: Timeout pattern
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
 * @file    timeout.c
 * @brief   TikuOS Example 08 - Timeout Pattern
 *
 * Demonstrates a common embedded pattern: start an operation and
 * wait for completion, but give up if it takes too long.
 *
 * The process simulates waiting for a button press within a
 * 5-second window.  If the button is pressed in time, LED1 turns
 * on (success).  If the timeout expires first, LED2 turns on
 * (failure).  The cycle then repeats.
 *
 * Hardware: MSP430FR5969 LaunchPad
 *   Button S1 = P4.5
 *   LED1      = P1.0  (success indicator)
 *   LED2      = P4.6  (timeout indicator)
 *
 * What you learn:
 *   - Combining timer events with other event sources
 *   - Cancelling a timer on early completion
 *   - Implementing timeouts for asynchronous operations
 */

#include "tiku.h"

#if TIKU_EXAMPLE_TIMEOUT

/*--------------------------------------------------------------------------*/
/* Custom event                                                              */
/*--------------------------------------------------------------------------*/
#define EVENT_BUTTON_PRESS    (TIKU_EVENT_USER + 0)

/*--------------------------------------------------------------------------*/
/* Process declarations                                                      */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(timeout_demo, "Timeout Demo");
TIKU_PROCESS(button_poller, "Button Poller");

/*--------------------------------------------------------------------------*/
/* Button poller -- polls button and posts event on press                     */
/*--------------------------------------------------------------------------*/
static struct tiku_timer btn_poll_timer;

TIKU_PROCESS_THREAD(button_poller, ev, data)
{
    static uint8_t prev = 0;

    TIKU_PROCESS_BEGIN();

    TIKU_BOARD_BTN1_INIT();
    tiku_timer_set_event(&btn_poll_timer, TIKU_CLOCK_MS_TO_TICKS(50));

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        uint8_t cur = TIKU_BOARD_BTN1_PRESSED();
        if (cur && !prev) {
            tiku_process_post(&timeout_demo, EVENT_BUTTON_PRESS, NULL);
        }
        prev = cur;

        tiku_timer_reset(&btn_poll_timer);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Timeout demo -- waits for button press or timeout                         */
/*--------------------------------------------------------------------------*/
static struct tiku_timer timeout_timer;
static struct tiku_timer restart_timer;

TIKU_PROCESS_THREAD(timeout_demo, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_common_led1_init();
    tiku_common_led2_init();

    while (1) {
        /* Reset LEDs */
        tiku_common_led1_off();
        tiku_common_led2_off();

        /* Start 5-second timeout */
        tiku_timer_set_event(&timeout_timer, TIKU_CLOCK_SECOND * 5);

        /* Wait for either button press or timeout */
        TIKU_PROCESS_WAIT_EVENT();

        if (ev == EVENT_BUTTON_PRESS) {
            /* Success -- button pressed in time */
            tiku_timer_stop(&timeout_timer);
            tiku_common_led1_on();
        } else if (ev == TIKU_EVENT_TIMER) {
            /* Timeout -- no button press */
            tiku_common_led2_on();
        }

        /* Show result for 2 seconds, then restart */
        tiku_timer_set_event(&restart_timer, TIKU_CLOCK_SECOND * 2);
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart                                                                 */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&timeout_demo, &button_poller);

#endif /* TIKU_EXAMPLE_TIMEOUT */
