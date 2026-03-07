/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_process_graceful_exit.c - Graceful exit vs force exit test
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

#if TEST_PROCESS_GRACEFUL_EXIT

static volatile unsigned int ge_cleanup_done = 0;
static volatile unsigned int fe_cleanup_done = 0;

TIKU_PROCESS(test_graceful_proc, "graceful");
TIKU_PROCESS(test_force_proc, "force");

TIKU_PROCESS_THREAD(test_graceful_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();

        if (ev == TIKU_EVENT_EXIT) {
            /* Graceful: do cleanup, then explicitly exit */
            ge_cleanup_done = 1;
            TEST_PRINTF("Graceful proc: cleanup done, exiting\n");
            TIKU_PROCESS_EXIT();
        }
    }

    TIKU_PROCESS_END();
}

TIKU_PROCESS_THREAD(test_force_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();

        if (ev == TIKU_EVENT_FORCE_EXIT) {
            /* This cleanup runs but the process will be killed
             * regardless of what we return */
            fe_cleanup_done = 1;
            TEST_PRINTF("Force proc: got force exit\n");
            /* Don't call TIKU_PROCESS_EXIT — just yield.
             * The scheduler should kill us anyway. */
        }
    }

    TIKU_PROCESS_END();
}

void test_process_graceful_exit(void)
{
    TEST_PRINTF("\n=== Test: Graceful Exit vs Force Exit ===\n");

    ge_cleanup_done = 0;
    fe_cleanup_done = 0;

    tiku_process_init();

    /*---------------------------------------------------------------*/
    /* Part A: TIKU_EVENT_EXIT — process decides when to die         */
    /*---------------------------------------------------------------*/

    tiku_process_start(&test_graceful_proc, NULL);
    while (tiku_process_run()) {
        /* drain INIT */
    }

    /* Send polite exit request */
    tiku_process_post(&test_graceful_proc, TIKU_EVENT_EXIT, NULL);
    while (tiku_process_run()) {
        /* drain */
    }

    if (ge_cleanup_done == 1) {
        TEST_PRINTF("PASS: Graceful process ran cleanup\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Graceful process did not run cleanup\n");
    }

    if (!test_graceful_proc.is_running) {
        TEST_PRINTF("PASS: Graceful process exited after cleanup\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Graceful process still running\n");
    }

    /*---------------------------------------------------------------*/
    /* Part B: TIKU_EVENT_FORCE_EXIT — unconditional kill            */
    /*---------------------------------------------------------------*/

    tiku_process_start(&test_force_proc, NULL);
    while (tiku_process_run()) {
        /* drain INIT */
    }

    /* Send force exit — process does NOT call TIKU_PROCESS_EXIT */
    tiku_process_post(&test_force_proc, TIKU_EVENT_FORCE_EXIT, NULL);
    while (tiku_process_run()) {
        /* drain */
    }

    if (fe_cleanup_done == 1) {
        TEST_PRINTF("PASS: Force process thread body executed\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Force process thread body did not run\n");
    }

    /* Process must be killed even though thread didn't PT_EXIT */
    if (!test_force_proc.is_running) {
        TEST_PRINTF("PASS: Force process killed unconditionally\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Force process still running\n");
    }

    TEST_PRINTF("Graceful/force exit test completed\n\n");
}

#endif /* TEST_PROCESS_GRACEFUL_EXIT */

#endif /* PLATFORM_MSP430 */
