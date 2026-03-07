/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_process_events.c - Event posting test
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

#if TEST_PROCESS_EVENTS

static volatile unsigned int event_recv_count = 0;

TIKU_PROCESS(test_event_proc, "test_events");

TIKU_PROCESS_THREAD(test_event_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();

        if (ev == TEST_EVENT_CUSTOM) {
            event_recv_count++;
            TEST_PRINTF("Event process: received custom event #%d\n",
                         event_recv_count);
        }
    }

    TIKU_PROCESS_END();
}

void test_process_events(void)
{
    unsigned int i;
    unsigned int posted;

    TEST_PRINTF("\n=== Test: Event Posting ===\n");

    event_recv_count = 0;

    tiku_process_init();
    tiku_process_start(&test_event_proc, NULL);

    /* Drain the INIT event */
    while (tiku_process_run()) {
        /* drain */
    }

    /* Post multiple custom events */
    for (i = 0; i < TEST_NUM_EVENTS; i++) {
        posted = tiku_process_post(&test_event_proc,
                                    TEST_EVENT_CUSTOM, NULL);
        if (!posted) {
            TEST_PRINTF("FAIL: Could not post event %d (queue full)\n", i);
        }
    }

    /* Run scheduler to deliver all events */
    while (tiku_process_run()) {
        /* drain */
    }

    if (event_recv_count == TEST_NUM_EVENTS) {
        TEST_PRINTF("PASS: All %d custom events received\n",
                     TEST_NUM_EVENTS);
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected %d events, got %d\n",
                     TEST_NUM_EVENTS, event_recv_count);
    }

    /* Verify queue return value: post should succeed */
    posted = tiku_process_post(&test_event_proc,
                                TEST_EVENT_CUSTOM, NULL);
    if (posted) {
        TEST_PRINTF("PASS: Post returns 1 on success\n");
    } else {
        TEST_PRINTF("FAIL: Post returned 0 unexpectedly\n");
    }

    /* Clean up */
    tiku_process_exit(&test_event_proc);
    TEST_PRINTF("Event posting test completed\n\n");
}

#endif /* TEST_PROCESS_EVENTS */

#endif /* PLATFORM_MSP430 */
