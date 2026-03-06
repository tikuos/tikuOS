/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_timer_stop.c - Timer stop test
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

#if TEST_TIMER_STOP

static volatile unsigned int stop_callback_fired = 0;
static struct tiku_timer stop_tmr;

static void test_stop_func(void *ptr)
{
    stop_callback_fired = 1;
    TEST_PRINTF("Stop timer: callback fired (unexpected!)\n");
}

void test_timer_stop(void)
{
    unsigned int loops;

    TEST_PRINTF("\n=== Test: Timer Stop ===\n");

    stop_callback_fired = 0;

    tiku_process_init();
    tiku_timer_init();
    tiku_clock_init();

    /* Set a 1-second callback timer */
    tiku_timer_set_callback(&stop_tmr, TEST_TIMER_INTERVAL,
                            test_stop_func, NULL);
    TEST_PRINTF("Stop timer: set for %u ticks\n",
                (unsigned int)TEST_TIMER_INTERVAL);

    /* Verify timer is active */
    if (!tiku_timer_expired(&stop_tmr)) {
        TEST_PRINTF("PASS: Timer is active after set\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Timer reports expired right after set\n");
    }

    /* Stop it before it fires */
    tiku_timer_stop(&stop_tmr);
    TEST_PRINTF("Stop timer: stopped\n");

    /* Verify timer reports expired (removed from active list) */
    if (tiku_timer_expired(&stop_tmr)) {
        TEST_PRINTF("PASS: Timer reports expired after stop\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Timer still active after stop\n");
    }

    /* Run the scheduler past the original expiration to confirm */
    for (loops = 0; loops < TEST_TIMER_DRAIN_MAX; loops++) {
        tiku_clock_wait(1);
        tiku_timer_request_poll();
        while (tiku_process_run()) {
            /* drain */
        }
    }

    if (stop_callback_fired == 0) {
        TEST_PRINTF("PASS: Callback did not fire after stop\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Callback fired despite stop\n");
    }

    TEST_PRINTF("Timer stop test completed\n\n");
}

#endif /* TEST_TIMER_STOP */

#endif /* PLATFORM_MSP430 */
