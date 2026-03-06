/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_timer_periodic.c - Periodic timer (drift-free reset) test
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

#if TEST_TIMER_PERIODIC

static volatile unsigned int periodic_count = 0;

static struct tiku_timer periodic_tmr;

static void test_periodic_func(void *ptr)
{
    periodic_count++;
    TEST_PRINTF("Periodic timer: tick %u/%u\n",
                periodic_count, TEST_TIMER_PERIODIC_CNT);

    if (periodic_count < TEST_TIMER_PERIODIC_CNT) {
        /* Drift-free reschedule */
        tiku_timer_reset(&periodic_tmr);
    }
}

TIKU_PROCESS(test_periodic_timer_proc, "test_per_tmr");

TIKU_PROCESS_THREAD(test_periodic_timer_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_timer_set_callback(&periodic_tmr, TEST_TIMER_SHORT,
                            test_periodic_func, NULL);
    TEST_PRINTF("Periodic timer: set for %u ticks, %u repeats\n",
                (unsigned int)TEST_TIMER_SHORT, TEST_TIMER_PERIODIC_CNT);

    /* Keep alive while the periodic timer runs */
    while (periodic_count < TEST_TIMER_PERIODIC_CNT) {
        TIKU_PROCESS_WAIT_EVENT();
    }

    TIKU_PROCESS_END();
}

void test_timer_periodic(void)
{
    unsigned int loops;

    TEST_PRINTF("\n=== Test: Periodic Timer ===\n");

    periodic_count = 0;

    tiku_process_init();
    tiku_timer_init();
    tiku_clock_init();
    tiku_process_start(&test_periodic_timer_proc, NULL);

    /* Drain INIT */
    while (tiku_process_run()) {
        /* drain */
    }

    TEST_PRINTF("Periodic timer: waiting for %u callbacks...\n",
                TEST_TIMER_PERIODIC_CNT);

    for (loops = 0; loops < TEST_TIMER_DRAIN_MAX * TEST_TIMER_PERIODIC_CNT;
         loops++) {
        tiku_clock_wait(1);
        tiku_timer_request_poll();
        while (tiku_process_run()) {
            /* drain */
        }
        if (periodic_count >= TEST_TIMER_PERIODIC_CNT) {
            break;
        }
    }

    if (periodic_count == TEST_TIMER_PERIODIC_CNT) {
        TEST_PRINTF("PASS: Periodic timer fired %u times\n",
                     TEST_TIMER_PERIODIC_CNT);
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Periodic count = %u (expected %u)\n",
                     periodic_count, TEST_TIMER_PERIODIC_CNT);
    }

    /* Verify timer is no longer active (stopped after last tick) */
    if (tiku_timer_expired(&periodic_tmr)) {
        TEST_PRINTF("PASS: Timer stopped after final tick\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Timer still active after final tick\n");
    }

    tiku_process_exit(&test_periodic_timer_proc);
    TEST_PRINTF("Periodic timer test completed\n\n");
}

#endif /* TEST_TIMER_PERIODIC */

#endif /* PLATFORM_MSP430 */
