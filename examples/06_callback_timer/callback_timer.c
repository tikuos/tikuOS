/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * callback_timer.c - TikuOS Example 06: Callback-mode timers
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
 * @file    callback_timer.c
 * @brief   TikuOS Example 06 - Callback Timers
 *
 * Demonstrates callback-mode timers.  Instead of posting an event
 * to a process, the timer calls a function directly when it expires.
 * This is useful for simple periodic actions that don't need a
 * full process context.
 *
 * Two callback timers run independently:
 *   Timer A -- toggles LED1 every 500 ms
 *   Timer B -- toggles LED2 every 1.3 s
 *
 * The main process just idles after setup.
 *
 * Hardware: MSP430FR5969 LaunchPad (LED1 = P1.0, LED2 = P4.6)
 *
 * What you learn:
 *   - Setting callback-mode timers with tiku_timer_set_callback()
 *   - Rescheduling inside the callback for periodic behavior
 *   - Comparing callback timers vs event timers
 */

#include "tiku.h"

#if TIKU_EXAMPLE_CALLBACK_TIMER

/*--------------------------------------------------------------------------*/
/* Timer structures (must be static / persist)                               */
/*--------------------------------------------------------------------------*/
static struct tiku_timer timer_a;
static struct tiku_timer timer_b;

/*--------------------------------------------------------------------------*/
/* Callbacks                                                                 */
/*--------------------------------------------------------------------------*/
static void on_timer_a(void *ptr)
{
    (void)ptr;
    tiku_common_led1_toggle();
    tiku_timer_reset(&timer_a);  /* Periodic */
}

static void on_timer_b(void *ptr)
{
    (void)ptr;
    tiku_common_led2_toggle();
    tiku_timer_reset(&timer_b);  /* Periodic */
}

/*--------------------------------------------------------------------------*/
/* Process -- sets up timers then idles                                       */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(callback_demo, "Callback Demo");

TIKU_PROCESS_THREAD(callback_demo, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_common_led1_init();
    tiku_common_led2_init();

    /* Start callback timers */
    tiku_timer_set_callback(&timer_a, TIKU_CLOCK_SECOND / 2,
                            on_timer_a, NULL);
    tiku_timer_set_callback(&timer_b,
                            TIKU_CLOCK_SECOND + TIKU_CLOCK_SECOND * 3 / 10,
                            on_timer_b, NULL);

    /* Nothing else to do -- callbacks handle everything */
    while (1) {
        TIKU_PROCESS_WAIT_EVENT();
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart                                                                 */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&callback_demo);

#endif /* TIKU_EXAMPLE_CALLBACK_TIMER */
