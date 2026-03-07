/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_htimer_periodic.c - Hardware timer periodic (self-reschedule) test
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

#if TEST_HTIMER_PERIODIC

static volatile unsigned int htimer_periodic_count = 0;
static struct tiku_htimer periodic_ht;

static void test_htimer_periodic_cb(struct tiku_htimer *t, void *ptr)
{
    htimer_periodic_count++;
    TEST_PRINTF("HTimer periodic: tick %u/%u\n",
                htimer_periodic_count, TEST_HTIMER_REPEAT_CNT);

    if (htimer_periodic_count < TEST_HTIMER_REPEAT_CNT) {
        /* Drift-free reschedule from the scheduled time */
        tiku_htimer_set(t, TIKU_HTIMER_TIME(t) + TEST_HTIMER_PERIOD,
                        test_htimer_periodic_cb, NULL);
    }
}

void test_htimer_periodic(void)
{
    int ret;
    tiku_htimer_clock_t target;
    unsigned int wait_loops;

    TEST_PRINTF("\n=== Test: Hardware Timer Periodic ===\n");

    htimer_periodic_count = 0;

    tiku_htimer_init();

    /* Schedule first tick 100ms from now */
    target = TIKU_HTIMER_NOW() + TEST_HTIMER_PERIOD;
    ret = tiku_htimer_set(&periodic_ht, target,
                          test_htimer_periodic_cb, NULL);

    if (ret == TIKU_HTIMER_OK) {
        TEST_PRINTF("HTimer periodic: started, expecting %u ticks\n",
                     TEST_HTIMER_REPEAT_CNT);
    } else {
        TEST_PRINTF("FAIL: tiku_htimer_set returned %d\n", ret);
        return;
    }

    /* Wait for all periodic ticks (busy-wait with timeout) */
    for (wait_loops = 0; wait_loops < 500000; wait_loops++) {
        if (htimer_periodic_count >= TEST_HTIMER_REPEAT_CNT) {
            break;
        }
        __no_operation();
    }

    if (htimer_periodic_count == TEST_HTIMER_REPEAT_CNT) {
        TEST_PRINTF("PASS: HTimer periodic completed %u ticks\n",
                     TEST_HTIMER_REPEAT_CNT);
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: HTimer periodic count = %u (expected %u)\n",
                     htimer_periodic_count, TEST_HTIMER_REPEAT_CNT);
    }

    /* After final tick, no reschedule, so nothing pending */
    if (!tiku_htimer_is_scheduled()) {
        TEST_PRINTF("PASS: No htimer scheduled after final tick\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: HTimer still scheduled after final tick\n");
        tiku_htimer_cancel();
    }

    TEST_PRINTF("Hardware timer periodic test completed\n\n");
}

#endif /* TEST_HTIMER_PERIODIC */

#endif /* PLATFORM_MSP430 */
