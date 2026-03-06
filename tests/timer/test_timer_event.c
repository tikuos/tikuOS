/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_timer_event.c - Event timer test
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

#include "test_timer.h"

#ifdef PLATFORM_MSP430

#if TEST_TIMER_EVENT

static volatile unsigned int event_timer_fired = 0;
static struct tiku_timer event_tmr;

TIKU_PROCESS(test_event_timer_proc, "test_evt_tmr");

TIKU_PROCESS_THREAD(test_event_timer_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    /* Set a 1-second event timer */
    tiku_timer_set_event(&event_tmr, TEST_TIMER_INTERVAL);
    TEST_PRINTF("Event timer: set for %u ticks\n",
                (unsigned int)TEST_TIMER_INTERVAL);

    /* Wait for the timer event */
    TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

    event_timer_fired = 1;
    TEST_PRINTF("Event timer: received TIKU_EVENT_TIMER\n");

    TIKU_PROCESS_END();
}

void test_timer_event(void)
{
    unsigned int loops;

    TEST_PRINTF("\n=== Test: Event Timer ===\n");

    event_timer_fired = 0;

    tiku_process_init();
    tiku_timer_init();
    tiku_clock_init();
    tiku_process_start(&test_event_timer_proc, NULL);

    /* Drain INIT and let the process set its timer */
    while (tiku_process_run()) {
        /* drain */
    }

    TEST_PRINTF("Event timer: waiting for expiration...\n");

    /* Poll until timer fires or we time out */
    for (loops = 0; loops < TEST_TIMER_DRAIN_MAX; loops++) {
        tiku_clock_wait(1);
        tiku_timer_request_poll();
        while (tiku_process_run()) {
            /* drain */
        }
        if (event_timer_fired) {
            break;
        }
    }

    if (event_timer_fired) {
        TEST_PRINTF("PASS: Event timer fired after %u poll loops\n", loops);
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Event timer did not fire within %u loops\n",
                     TEST_TIMER_DRAIN_MAX);
    }

    /* Verify timer is no longer active */
    if (tiku_timer_expired(&event_tmr)) {
        TEST_PRINTF("PASS: Timer reports expired\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Timer still reports active\n");
    }

    TEST_PRINTF("Event timer test completed\n\n");
}

#endif /* TEST_TIMER_EVENT */

#endif /* PLATFORM_MSP430 */
