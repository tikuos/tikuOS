/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_process_broadcast_exit.c - Broadcast exit safety test
 *
 * Verifies fix for broadcast list corruption: if a process exits while
 * the scheduler is iterating the process list during a broadcast, the
 * remaining processes must still receive the event.
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

#include "test_process.h"

#ifdef PLATFORM_MSP430

#if TEST_PROCESS_BROADCAST_EXIT

static volatile unsigned int bce_count_a = 0;
static volatile unsigned int bce_count_b = 0;
static volatile unsigned int bce_count_c = 0;

TIKU_PROCESS(test_bce_proc_a, "bce_a");
TIKU_PROCESS(test_bce_proc_b, "bce_b");
TIKU_PROCESS(test_bce_proc_c, "bce_c");

TIKU_PROCESS_THREAD(test_bce_proc_a, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();

        if (ev == TEST_EVENT_CUSTOM) {
            bce_count_a++;
            TEST_PRINTF("BCE A: received event\n");
        }
    }

    TIKU_PROCESS_END();
}

TIKU_PROCESS_THREAD(test_bce_proc_b, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();

        if (ev == TEST_EVENT_CUSTOM) {
            bce_count_b++;
            TEST_PRINTF("BCE B: received event, exiting\n");
            TIKU_PROCESS_EXIT();    /* unlinks B during broadcast */
        }
    }

    TIKU_PROCESS_END();
}

TIKU_PROCESS_THREAD(test_bce_proc_c, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();

        if (ev == TEST_EVENT_CUSTOM) {
            bce_count_c++;
            TEST_PRINTF("BCE C: received event\n");
        }
    }

    TIKU_PROCESS_END();
}

void test_process_broadcast_exit(void)
{
    TEST_PRINTF("\n=== Test: Broadcast Exit Safety ===\n");

    bce_count_a = 0;
    bce_count_b = 0;
    bce_count_c = 0;

    tiku_process_init();
    tiku_process_start(&test_bce_proc_a, NULL);
    tiku_process_start(&test_bce_proc_b, NULL);
    tiku_process_start(&test_bce_proc_c, NULL);

    /* Drain INIT events */
    while (tiku_process_run()) {
        /* drain */
    }

    /* Broadcast: B will exit itself mid-iteration */
    tiku_process_post(TIKU_PROCESS_BROADCAST, TEST_EVENT_CUSTOM, NULL);
    while (tiku_process_run()) {
        /* drain */
    }

    /* A must have received the event */
    if (bce_count_a == 1) {
        TEST_PRINTF("PASS: Process A received broadcast\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Process A count = %d (expected 1)\n",
                     bce_count_a);
    }

    /* B received it and then exited */
    if (bce_count_b == 1) {
        TEST_PRINTF("PASS: Process B received broadcast before exit\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Process B count = %d (expected 1)\n",
                     bce_count_b);
    }

    /* B must actually be stopped */
    if (!test_bce_proc_b.is_running) {
        TEST_PRINTF("PASS: Process B exited during broadcast\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Process B still running\n");
    }

    /* C must have received the event despite B unlinking mid-iteration */
    if (bce_count_c == 1) {
        TEST_PRINTF("PASS: Process C received broadcast after B exited\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Process C count = %d (expected 1)\n",
                     bce_count_c);
    }

    /* Clean up */
    tiku_process_exit(&test_bce_proc_a);
    tiku_process_exit(&test_bce_proc_c);
    TEST_PRINTF("Broadcast exit safety test completed\n\n");
}

#endif /* TEST_PROCESS_BROADCAST_EXIT */

#endif /* PLATFORM_MSP430 */
