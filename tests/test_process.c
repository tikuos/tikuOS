/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_process.c - Process subsystem tests
 *
 * Tests the event-driven cooperative multitasking system:
 * 1. Process lifecycle (start, init event, exit)
 * 2. Event posting and delivery
 * 3. Cooperative yielding across multiple phases
 * 4. Broadcast event delivery to multiple processes
 * 5. Process poll mechanism
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

/*---------------------------------------------------------------------------*/
/* PRIVATE CONSTANTS                                                         */
/*---------------------------------------------------------------------------*/

#define TEST_EVENT_CUSTOM   (TIKU_EVENT_USER + 1)
#define TEST_NUM_EVENTS     5
#define TEST_NUM_POLLS      3

/*---------------------------------------------------------------------------*/
/* TEST 1: BASIC PROCESS LIFECYCLE                                           */
/*---------------------------------------------------------------------------*/

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

/*---------------------------------------------------------------------------*/
/* TEST 2: EVENT POSTING                                                     */
/*---------------------------------------------------------------------------*/

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

/*---------------------------------------------------------------------------*/
/* TEST 3: COOPERATIVE YIELD                                                 */
/*---------------------------------------------------------------------------*/

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

/*---------------------------------------------------------------------------*/
/* TEST 4: BROADCAST EVENTS                                                  */
/*---------------------------------------------------------------------------*/

#if TEST_PROCESS_BROADCAST

static volatile unsigned int bcast_count_a = 0;
static volatile unsigned int bcast_count_b = 0;

TIKU_PROCESS(test_bcast_proc_a, "bcast_a");
TIKU_PROCESS(test_bcast_proc_b, "bcast_b");

TIKU_PROCESS_THREAD(test_bcast_proc_a, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();

        if (ev == TEST_EVENT_CUSTOM) {
            bcast_count_a++;
            TEST_PRINTF("Broadcast A: received event #%d\n",
                         bcast_count_a);
        }
    }

    TIKU_PROCESS_END();
}

TIKU_PROCESS_THREAD(test_bcast_proc_b, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();

        if (ev == TEST_EVENT_CUSTOM) {
            bcast_count_b++;
            TEST_PRINTF("Broadcast B: received event #%d\n",
                         bcast_count_b);
        }
    }

    TIKU_PROCESS_END();
}

void test_process_broadcast(void)
{
    TEST_PRINTF("\n=== Test: Broadcast Events ===\n");

    bcast_count_a = 0;
    bcast_count_b = 0;

    tiku_process_init();
    tiku_process_start(&test_bcast_proc_a, NULL);
    tiku_process_start(&test_bcast_proc_b, NULL);

    /* Drain INIT events for both processes */
    while (tiku_process_run()) {
        /* drain */
    }

    /* Post a broadcast event (NULL target = all processes) */
    tiku_process_post(TIKU_PROCESS_BROADCAST, TEST_EVENT_CUSTOM, NULL);

    /* Run scheduler to deliver broadcast */
    while (tiku_process_run()) {
        /* drain */
    }

    if (bcast_count_a == 1) {
        TEST_PRINTF("PASS: Process A received broadcast\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Process A count = %d (expected 1)\n",
                     bcast_count_a);
    }

    if (bcast_count_b == 1) {
        TEST_PRINTF("PASS: Process B received broadcast\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Process B count = %d (expected 1)\n",
                     bcast_count_b);
    }

    /* Post a second broadcast to verify repeated delivery */
    tiku_process_post(TIKU_PROCESS_BROADCAST, TEST_EVENT_CUSTOM, NULL);
    while (tiku_process_run()) {
        /* drain */
    }

    if (bcast_count_a == 2 && bcast_count_b == 2) {
        TEST_PRINTF("PASS: Both processes received second broadcast\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Counts after 2nd broadcast: A=%d B=%d\n",
                     bcast_count_a, bcast_count_b);
    }

    /* Clean up */
    tiku_process_exit(&test_bcast_proc_a);
    tiku_process_exit(&test_bcast_proc_b);
    TEST_PRINTF("Broadcast event test completed\n\n");
}

#endif /* TEST_PROCESS_BROADCAST */

/*---------------------------------------------------------------------------*/
/* TEST 5: PROCESS POLL                                                      */
/*---------------------------------------------------------------------------*/

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
