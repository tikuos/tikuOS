/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * channel.c - TikuOS Example 09: Channel-based message passing
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
 * @file    channel.c
 * @brief   TikuOS Example 09 - Channel-Based Message Passing
 *
 * A sensor process periodically produces a reading (a simple
 * struct with a sequence number and value) and pushes it into
 * a channel.  A display process waits for a notification event,
 * drains all available messages from the channel, and toggles
 * an LED for each message consumed.
 *
 * Channels decouple producers and consumers: the producer never
 * needs to know which process will read the data, and the
 * consumer can drain multiple buffered messages in one go.
 *
 * Hardware: MSP430FR5969 LaunchPad
 *   LED1 = P1.0  -- toggled by consumer for every message read
 *   LED2 = P4.6  -- toggled by producer for every message sent
 *
 * What you learn:
 *   - Declaring a type-safe channel with TIKU_CHANNEL_DECLARE
 *   - Putting and getting typed messages
 *   - Combining channels with events to wake a consumer
 *   - Querying channel state (empty / free slots)
 */

#include "tiku.h"

#if TIKU_EXAMPLE_CHANNEL

/*--------------------------------------------------------------------------*/
/* Message type                                                              */
/*--------------------------------------------------------------------------*/

/** @brief Simple sensor reading shared through the channel */
struct sensor_msg {
    uint16_t seq;       /**< Sequence number */
    uint16_t value;     /**< Simulated sensor value */
};

/*--------------------------------------------------------------------------*/
/* Channel declaration (type-safe)                                           */
/*--------------------------------------------------------------------------*/
TIKU_CHANNEL_DECLARE(sensor_ch, struct sensor_msg, 4);

/*--------------------------------------------------------------------------*/
/* Custom event                                                              */
/*--------------------------------------------------------------------------*/
#define EVENT_DATA_READY   (TIKU_EVENT_USER + 0)

/*--------------------------------------------------------------------------*/
/* Process declarations                                                      */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(sensor_proc, "Sensor");
TIKU_PROCESS(display_proc, "Display");

/*--------------------------------------------------------------------------*/
/* Sensor -- produces a reading every second                                 */
/*--------------------------------------------------------------------------*/
static struct tiku_timer sensor_timer;

TIKU_PROCESS_THREAD(sensor_proc, ev, data)
{
    static uint16_t seq = 0;

    TIKU_PROCESS_BEGIN();

    tiku_common_led2_init();
    sensor_ch_init();
    tiku_timer_set_event(&sensor_timer, TIKU_CLOCK_SECOND);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        /* Build a message */
        struct sensor_msg msg;
        msg.seq   = seq++;
        msg.value = seq * 10;   /* simulated reading */

        /* Try to put it into the channel (type-safe) */
        if (sensor_ch_put(&msg)) {
            /* Notify the consumer that data is available */
            tiku_process_post(&display_proc, EVENT_DATA_READY, NULL);
            tiku_common_led2_toggle();
        }
        /* If channel is full the message is simply dropped;
         * the consumer will catch up on the next drain cycle. */

        tiku_timer_reset(&sensor_timer);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Display -- drains all buffered messages when notified                      */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS_THREAD(display_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_common_led1_init();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == EVENT_DATA_READY);

        /* Drain every pending message in one go */
        struct sensor_msg msg;
        while (sensor_ch_get(&msg)) {
            /* Use msg.seq / msg.value here -- e.g., display,
             * threshold check, or logging via UART.          */
            (void)msg;

            tiku_common_led1_toggle();
        }
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart                                                                 */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&sensor_proc, &display_proc);

#endif /* TIKU_EXAMPLE_CHANNEL */
