/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * multi_process.c - TikuOS Example 04: Inter-process events
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
 * @file    multi_process.c
 * @brief   TikuOS Example 04 - Multi-Process Communication
 *
 * Two processes communicate via events.  A "producer" periodically
 * posts user events with an incrementing counter.  A "consumer"
 * receives them and indicates activity by toggling an LED.
 *
 * Hardware: MSP430FR5969 LaunchPad
 *   LED1 = P1.0  -- toggled by consumer on every received event
 *   LED2 = P4.6  -- toggled by producer on every post
 *
 * What you learn:
 *   - Posting custom events to a specific process
 *   - Receiving events and extracting data
 *   - Defining custom event IDs
 *   - Coordinating two processes
 */

#include "tiku.h"

#if TIKU_EXAMPLE_MULTI_PROCESS

/*--------------------------------------------------------------------------*/
/* Custom event                                                              */
/*--------------------------------------------------------------------------*/
#define EVENT_NEW_DATA    (TIKU_EVENT_USER + 0)

/*--------------------------------------------------------------------------*/
/* Process declarations                                                      */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(producer, "Producer");
TIKU_PROCESS(consumer, "Consumer");

/*--------------------------------------------------------------------------*/
/* Producer -- sends counter value every 2 seconds                           */
/*--------------------------------------------------------------------------*/
static struct tiku_timer producer_timer;

TIKU_PROCESS_THREAD(producer, ev, data)
{
    static int counter = 0;

    TIKU_PROCESS_BEGIN();

    tiku_common_led2_init();
    tiku_timer_set_event(&producer_timer, TIKU_CLOCK_SECOND * 2);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        /* Post data to the consumer */
        tiku_process_post(&consumer, EVENT_NEW_DATA, (void *)(intptr_t)counter);
        counter++;

        tiku_common_led2_toggle();
        tiku_timer_reset(&producer_timer);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Consumer -- receives data and acts on it                                  */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS_THREAD(consumer, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_common_led1_init();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == EVENT_NEW_DATA);

        /* Extract the counter value */
        int value = (int)(intptr_t)data;
        (void)value;  /* Use it -- e.g., threshold check */

        /* Indicate reception */
        tiku_common_led1_toggle();
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart both                                                            */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&producer, &consumer);

#endif /* TIKU_EXAMPLE_MULTI_PROCESS */
