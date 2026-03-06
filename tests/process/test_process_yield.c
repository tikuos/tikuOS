/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_process_yield.c - Cooperative yield test
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

#if TEST_PROCESS_YIELD

static volatile unsigned int yield_phase = 0;

TIKU_PROCESS(test_yield_proc, "test_yield");

TIKU_PROCESS_THREAD(test_yield_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    /* Phase 1: first work unit */
    yield_phase = 1;
    TEST_PRINTF("Yield process: phase 1 (initial work)\n");
    TIKU_PROCESS_YIELD();

    /* Phase 2: second work unit after yield */
    yield_phase = 2;
    TEST_PRINTF("Yield process: phase 2 (resumed)\n");
    TIKU_PROCESS_YIELD();

    /* Phase 3: final work unit */
    yield_phase = 3;
    TEST_PRINTF("Yield process: phase 3 (final)\n");

    TIKU_PROCESS_END();
}

void test_process_yield(void)
{
    TEST_PRINTF("\n=== Test: Cooperative Yield ===\n");

    yield_phase = 0;

    tiku_process_init();
    tiku_process_start(&test_yield_proc, NULL);

    /* Deliver INIT -> process executes phase 1, yields */
    while (tiku_process_run()) {
        /* drain */
    }

    if (yield_phase == 1) {
        TEST_PRINTF("PASS: Phase 1 reached after INIT\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected phase 1, got %d\n", yield_phase);
    }

    /* Post event to resume -> phase 2, yields again */
    tiku_process_post(&test_yield_proc, TIKU_EVENT_CONTINUE, NULL);
    while (tiku_process_run()) {
        /* drain */
    }

    if (yield_phase == 2) {
        TEST_PRINTF("PASS: Phase 2 reached after first resume\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected phase 2, got %d\n", yield_phase);
    }

    /* Post event to resume -> phase 3, process ends */
    tiku_process_post(&test_yield_proc, TIKU_EVENT_CONTINUE, NULL);
    while (tiku_process_run()) {
        /* drain */
    }

    if (yield_phase == 3) {
        TEST_PRINTF("PASS: Phase 3 reached after second resume\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected phase 3, got %d\n", yield_phase);
    }

    if (!test_yield_proc.is_running) {
        TEST_PRINTF("PASS: Process ended after final phase\n");
    } else {
        TEST_PRINTF("FAIL: Process still running\n");
    }

    TEST_PRINTF("Cooperative yield test completed\n\n");
}

#endif /* TEST_PROCESS_YIELD */

#endif /* PLATFORM_MSP430 */
