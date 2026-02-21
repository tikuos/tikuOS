/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * dual_blink.c - TikuOS Example 02: Two LEDs, two processes
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
 * @file    dual_blink.c
 * @brief   TikuOS Example 02 - Dual LED Blink
 *
 * Two independent processes, each blinking a different LED at a
 * different rate.  Demonstrates cooperative multitasking -- both
 * processes share the CPU transparently.
 *
 * Hardware: MSP430FR5969 LaunchPad
 *   LED1 (red)   = P4.6  -- blinks every 500 ms
 *   LED2 (green) = P1.0  -- blinks every 1.5 s
 *
 * What you learn:
 *   - Running multiple processes concurrently
 *   - Each process has its own timer and state
 *   - TIKU_AUTOSTART_PROCESSES accepts a list
 */

#include "tiku.h"

#if TIKU_EXAMPLE_DUAL_BLINK

/*--------------------------------------------------------------------------*/
/* Process declarations                                                      */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(red_blink, "Red Blink");
TIKU_PROCESS(green_blink, "Green Blink");

/*--------------------------------------------------------------------------*/
/* Red LED -- 500 ms period (2 Hz)                                           */
/*--------------------------------------------------------------------------*/
static struct tiku_timer red_timer;

TIKU_PROCESS_THREAD(red_blink, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_common_led1_init();
    tiku_timer_set_event(&red_timer, TIKU_CLOCK_SECOND / 2);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
        tiku_common_led1_toggle();
        tiku_timer_reset(&red_timer);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Green LED -- 1.5 s period                                                 */
/*--------------------------------------------------------------------------*/
static struct tiku_timer green_timer;

TIKU_PROCESS_THREAD(green_blink, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_common_led2_init();
    tiku_timer_set_event(&green_timer, TIKU_CLOCK_SECOND + TIKU_CLOCK_SECOND / 2);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
        tiku_common_led2_toggle();
        tiku_timer_reset(&green_timer);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart both processes                                                  */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&red_blink, &green_blink);

#endif /* TIKU_EXAMPLE_DUAL_BLINK */
