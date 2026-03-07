/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_process_poll.c - Process poll mechanism test
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

#if TEST_PROCESS_POLL

static volatile unsigned int poll_count = 0;

TIKU_PROCESS(test_poll_proc, "test_poll");

TIKU_PROCESS_THREAD(test_poll_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();

        if (ev == TIKU_EVENT_POLL) {
            poll_count++;
            TEST_PRINTF("Poll process: received poll #%d\n", poll_count);
            tiku_common_led2_toggle();
        }
    }

    TIKU_PROCESS_END();
}

void test_process_poll(void)
{
    unsigned int i;

    TEST_PRINTF("\n=== Test: Process Poll ===\n");

    poll_count = 0;

    tiku_process_init();
    tiku_process_start(&test_poll_proc, NULL);

    /* Drain INIT */
    while (tiku_process_run()) {
        /* drain */
    }

    /* Request polls */
    for (i = 0; i < TEST_NUM_POLLS; i++) {
        tiku_process_poll(&test_poll_proc);
    }

    /* Run scheduler to deliver poll events */
    while (tiku_process_run()) {
        /* drain */
    }

    if (poll_count == TEST_NUM_POLLS) {
        TEST_PRINTF("PASS: Received all %d poll events\n", TEST_NUM_POLLS);
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected %d polls, got %d\n",
                     TEST_NUM_POLLS, poll_count);
    }

    /* Clean up */
    tiku_process_exit(&test_poll_proc);
    TEST_PRINTF("Process poll test completed\n\n");
}

#endif /* TEST_PROCESS_POLL */

#endif /* PLATFORM_MSP430 */
