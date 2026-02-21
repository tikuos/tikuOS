/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * broadcast.c - TikuOS Example 07: Broadcast events
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
 * @file    broadcast.c
 * @brief   TikuOS Example 07 - Broadcast Events
 *
 * A controller process broadcasts a user event to all processes.
 * Two listener processes each respond independently by toggling
 * their own LED.
 *
 * This pattern is useful when a single event (e.g., a sensor
 * reading or mode change) must notify many components at once.
 *
 * Hardware: MSP430FR5969 LaunchPad (LED1 = P1.0, LED2 = P4.6)
 *
 * What you learn:
 *   - Broadcasting events with TIKU_PROCESS_BROADCAST
 *   - Multiple processes reacting to the same event
 *   - Decoupled architecture (controller doesn't know listeners)
 */

#include "tiku.h"

#if TIKU_EXAMPLE_BROADCAST

/*--------------------------------------------------------------------------*/
/* Custom event                                                              */
/*--------------------------------------------------------------------------*/
#define EVENT_HEARTBEAT    (TIKU_EVENT_USER + 0)

/*--------------------------------------------------------------------------*/
/* Process declarations                                                      */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(controller, "Controller");
TIKU_PROCESS(listener_a, "Listener A");
TIKU_PROCESS(listener_b, "Listener B");

/*--------------------------------------------------------------------------*/
/* Controller -- broadcasts heartbeat every second                           */
/*--------------------------------------------------------------------------*/
static struct tiku_timer heartbeat_timer;

TIKU_PROCESS_THREAD(controller, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_timer_set_event(&heartbeat_timer, TIKU_CLOCK_SECOND);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        /* Broadcast to ALL running processes */
        tiku_process_post(TIKU_PROCESS_BROADCAST, EVENT_HEARTBEAT, NULL);

        tiku_timer_reset(&heartbeat_timer);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Listener A -- toggles LED1 on heartbeat                                   */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS_THREAD(listener_a, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_common_led1_init();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == EVENT_HEARTBEAT);
        tiku_common_led1_toggle();
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Listener B -- toggles LED2 on every other heartbeat                       */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS_THREAD(listener_b, ev, data)
{
    static int count = 0;

    TIKU_PROCESS_BEGIN();

    tiku_common_led2_init();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == EVENT_HEARTBEAT);
        count++;
        if (count % 2 == 0) {
            tiku_common_led2_toggle();
        }
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart                                                                 */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&controller, &listener_a, &listener_b);

#endif /* TIKU_EXAMPLE_BROADCAST */
