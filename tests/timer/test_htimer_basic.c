/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_htimer_basic.c - Hardware timer basic (one-shot) test
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

#if TEST_HTIMER_BASIC

static volatile unsigned int htimer_basic_fired = 0;
static struct tiku_htimer basic_ht;

static void test_htimer_basic_cb(struct tiku_htimer *t, void *ptr)
{
    htimer_basic_fired = 1;
    TEST_PRINTF("HTimer basic: ISR callback fired at %u\n",
                (unsigned int)TIKU_HTIMER_NOW());
}

void test_htimer_basic(void)
{
    int ret;
    tiku_htimer_clock_t target;
    unsigned int wait_loops;

    TEST_PRINTF("\n=== Test: Hardware Timer Basic ===\n");

    htimer_basic_fired = 0;

    tiku_htimer_init();

    /* Schedule 100ms from now */
    target = TIKU_HTIMER_NOW() + TEST_HTIMER_PERIOD;
    ret = tiku_htimer_set(&basic_ht, target, test_htimer_basic_cb, NULL);

    if (ret == TIKU_HTIMER_OK) {
        TEST_PRINTF("HTimer basic: scheduled at %u (now=%u)\n",
                     (unsigned int)target,
                     (unsigned int)TIKU_HTIMER_NOW());
    } else {
        TEST_PRINTF("FAIL: tiku_htimer_set returned %d\n", ret);
        return;
    }

    /* Verify it's scheduled */
    if (tiku_htimer_is_scheduled()) {
        TEST_PRINTF("PASS: HTimer reports scheduled\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: HTimer not scheduled after set\n");
    }

    /* Wait for ISR to fire (busy-wait with timeout) */
    for (wait_loops = 0; wait_loops < 50000; wait_loops++) {
        if (htimer_basic_fired) {
            break;
        }
        __no_operation();
    }

    if (htimer_basic_fired) {
        TEST_PRINTF("PASS: HTimer one-shot fired\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: HTimer did not fire within timeout\n");
    }

    /* After one-shot, nothing should be pending */
    if (!tiku_htimer_is_scheduled()) {
        TEST_PRINTF("PASS: No htimer scheduled after one-shot\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: HTimer still scheduled after one-shot\n");
    }

    TEST_PRINTF("Hardware timer basic test completed\n\n");
}

#endif /* TEST_HTIMER_BASIC */

#endif /* PLATFORM_MSP430 */
