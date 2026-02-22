/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * blink.c - TikuOS Example 01: Single LED blink
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
 * @file    blink.c
 * @brief   TikuOS Example 01 - LED Blink
 *
 * The simplest TikuOS application. A single process toggles LED1
 * once per second using a software event timer.
 *
 * Hardware: MSP430FR5969 LaunchPad (LED1 = P4.6, red)
 *
 * What you learn:
 *   - Declaring and defining a process
 *   - Using TIKU_AUTOSTART_PROCESSES for automatic startup
 *   - Setting an event timer and waiting for TIKU_EVENT_TIMER
 *   - Drift-free periodic scheduling with tiku_timer_reset()
 */

#include "tiku.h"

#if TIKU_EXAMPLE_BLINK

/*--------------------------------------------------------------------------*/
/* Process declaration                                                       */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(blink_process, "Blink");

/*--------------------------------------------------------------------------*/
/* Process thread                                                            */
/*--------------------------------------------------------------------------*/
static struct tiku_timer blink_timer;

TIKU_PROCESS_THREAD(blink_process, ev, data)
{
    TIKU_PROCESS_BEGIN();

    /* One-time initialization */
    tiku_common_led1_init();

    /* Start a 1-second periodic timer */
    tiku_timer_set_event(&blink_timer, TIKU_CLOCK_SECOND);

    while (1) {
        /* Block until the timer fires */
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        /* Toggle the LED */
        tiku_common_led1_toggle();

        /* Reset the timer (drift-free: next = previous + interval) */
        tiku_timer_reset(&blink_timer);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart                                                                 */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&blink_process);

#endif /* TIKU_EXAMPLE_BLINK */
