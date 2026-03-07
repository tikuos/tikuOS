/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_timer_callback.c - Callback timer test
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

#if TEST_TIMER_CALLBACK

static volatile unsigned int callback_count = 0;

static void test_callback_func(void *ptr)
{
    callback_count++;
    TEST_PRINTF("Callback timer: fired (count=%u)\n", callback_count);
}

static struct tiku_timer callback_tmr;

TIKU_PROCESS(test_callback_timer_proc, "test_cb_tmr");

TIKU_PROCESS_THREAD(test_callback_timer_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    /* Set a callback timer for 250ms */
    tiku_timer_set_callback(&callback_tmr, TEST_TIMER_SHORT,
                            test_callback_func, NULL);
    TEST_PRINTF("Callback timer: set for %u ticks\n",
                (unsigned int)TEST_TIMER_SHORT);

    /* Yield — callback will fire from the timer process */
    TIKU_PROCESS_WAIT_EVENT();

    TIKU_PROCESS_END();
}

void test_timer_callback(void)
{
    unsigned int loops;

    TEST_PRINTF("\n=== Test: Callback Timer ===\n");

    callback_count = 0;

    tiku_process_init();
    tiku_timer_init();
    tiku_clock_init();
    tiku_process_start(&test_callback_timer_proc, NULL);

    /* Drain INIT — process sets its callback timer */
    while (tiku_process_run()) {
        /* drain */
    }

    TEST_PRINTF("Callback timer: waiting for expiration...\n");

    /* Poll until callback fires */
    for (loops = 0; loops < TEST_TIMER_DRAIN_MAX; loops++) {
        tiku_clock_wait(1);
        tiku_timer_request_poll();
        while (tiku_process_run()) {
            /* drain */
        }
        if (callback_count > 0) {
            break;
        }
    }

    if (callback_count == 1) {
        TEST_PRINTF("PASS: Callback fired exactly once\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Callback count = %u (expected 1)\n",
                     callback_count);
    }

    tiku_process_exit(&test_callback_timer_proc);
    TEST_PRINTF("Callback timer test completed\n\n");
}

#endif /* TEST_TIMER_CALLBACK */

#endif /* PLATFORM_MSP430 */
