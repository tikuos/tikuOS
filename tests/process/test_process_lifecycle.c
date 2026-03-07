/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_process_lifecycle.c - Basic process lifecycle test
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

#if TEST_PROCESS_LIFECYCLE

static volatile unsigned int lifecycle_phase = 0;

TIKU_PROCESS(test_lifecycle_proc, "test_lifecycle");

TIKU_PROCESS_THREAD(test_lifecycle_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    /* Reached on first call (INIT event) */
    lifecycle_phase = 1;
    TEST_PRINTF("Lifecycle: initialized (phase 1)\n");

    TIKU_PROCESS_WAIT_EVENT();

    /* Reached on second event delivery */
    lifecycle_phase = 2;
    TEST_PRINTF("Lifecycle: continued (phase 2)\n");

    TIKU_PROCESS_END();
}

void test_process_lifecycle(void)
{
    TEST_PRINTF("\n=== Test: Basic Process Lifecycle ===\n");

    lifecycle_phase = 0;

    /* Initialize process system */
    tiku_process_init();
    TEST_PRINTF("Process system initialized\n");

    /* Start the process (posts INIT event) */
    tiku_process_start(&test_lifecycle_proc, NULL);
    TEST_PRINTF("Process started\n");

    /* Run scheduler to deliver INIT */
    while (tiku_process_run()) {
        /* drain */
    }

    /* Verify phase 1 reached */
    if (lifecycle_phase == 1) {
        TEST_PRINTF("PASS: Process received INIT event\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected phase 1, got %d\n", lifecycle_phase);
    }

    /* Verify process is still running (yielded, not ended) */
    if (test_lifecycle_proc.is_running) {
        TEST_PRINTF("PASS: Process is running after yield\n");
    } else {
        TEST_PRINTF("FAIL: Process is not running\n");
    }

    /* Post a continue event to advance past the yield */
    tiku_process_post(&test_lifecycle_proc, TIKU_EVENT_CONTINUE, NULL);
    while (tiku_process_run()) {
        /* drain */
    }

    /* Verify phase 2 reached and process auto-exited at PROCESS_END */
    if (lifecycle_phase == 2) {
        TEST_PRINTF("PASS: Process reached phase 2\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected phase 2, got %d\n", lifecycle_phase);
    }

    if (!test_lifecycle_proc.is_running) {
        TEST_PRINTF("PASS: Process auto-exited at PROCESS_END\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Process still running after PROCESS_END\n");
    }

    TEST_PRINTF("Process lifecycle test completed\n\n");
}

#endif /* TEST_PROCESS_LIFECYCLE */

#endif /* PLATFORM_MSP430 */
