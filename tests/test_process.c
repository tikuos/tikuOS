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

/*---------------------------------------------------------------------------*/
/* TEST 6: QUEUE QUERY FUNCTIONS                                             */
/*---------------------------------------------------------------------------*/

#if TEST_PROCESS_QUEUE

TIKU_PROCESS(test_queue_proc, "test_queue");

TIKU_PROCESS_THREAD(test_queue_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();
    }

    TIKU_PROCESS_END();
}

void test_process_queue_query(void)
{
    unsigned int i;
    uint8_t posted;

    TEST_PRINTF("\n=== Test: Queue Query Functions ===\n");

    /* Disable interrupts for the entire test so the timer ISR cannot
     * post events into the queue while we are checking its state. */
    tiku_atomic_enter();

    /* Fresh init — queue must be empty */
    tiku_process_init();

    /* --- Empty queue checks --- */
    if (tiku_process_queue_empty()) {
        TEST_PRINTF("PASS: Queue is empty after init\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Queue not empty after init\n");
    }

    if (tiku_process_queue_length() == 0) {
        TEST_PRINTF("PASS: Queue length is 0 after init\n");
    } else {
        TEST_PRINTF("FAIL: Queue length = %d (expected 0)\n",
                     tiku_process_queue_length());
    }

    if (tiku_process_queue_space() == TIKU_QUEUE_SIZE) {
        TEST_PRINTF("PASS: Queue space is %d after init\n",
                     TIKU_QUEUE_SIZE);
    } else {
        TEST_PRINTF("FAIL: Queue space = %d (expected %d)\n",
                     tiku_process_queue_space(), TIKU_QUEUE_SIZE);
    }

    if (!tiku_process_queue_full()) {
        TEST_PRINTF("PASS: Queue is not full after init\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Queue reports full after init\n");
    }

    /* --- Start process and post some events --- */
    tiku_process_start(&test_queue_proc, NULL);

    /* start posts an INIT event, so length should be 1 */
    if (tiku_process_queue_length() == 1) {
        TEST_PRINTF("PASS: Queue length is 1 after process start\n");
    } else {
        TEST_PRINTF("FAIL: Queue length = %d (expected 1)\n",
                     tiku_process_queue_length());
    }

    if (!tiku_process_queue_empty()) {
        TEST_PRINTF("PASS: Queue is not empty after post\n");
    } else {
        TEST_PRINTF("FAIL: Queue reports empty after post\n");
    }

    if (tiku_process_queue_space() == TIKU_QUEUE_SIZE - 1) {
        TEST_PRINTF("PASS: Queue space is %d after 1 post\n",
                     TIKU_QUEUE_SIZE - 1);
    } else {
        TEST_PRINTF("FAIL: Queue space = %d (expected %d)\n",
                     tiku_process_queue_space(), TIKU_QUEUE_SIZE - 1);
    }

    /* Drain INIT */
    while (tiku_process_run()) {
        /* drain */
    }

    /* Queue should be empty again after draining */
    if (tiku_process_queue_empty()) {
        TEST_PRINTF("PASS: Queue empty after draining events\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Queue not empty after drain\n");
    }

    /* --- Fill the queue completely --- */
    for (i = 0; i < TIKU_QUEUE_SIZE; i++) {
        posted = tiku_process_post(&test_queue_proc,
                                    TIKU_EVENT_CONTINUE, NULL);
        if (!posted) {
            TEST_PRINTF("FAIL: Could not post event %d\n", i);
            break;
        }
    }

    if (tiku_process_queue_full()) {
        TEST_PRINTF("PASS: Queue is full after %d posts\n",
                     TIKU_QUEUE_SIZE);
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Queue not full (length=%d, expected %d)\n",
                     tiku_process_queue_length(), TIKU_QUEUE_SIZE);
    }

    if (tiku_process_queue_length() == TIKU_QUEUE_SIZE) {
        TEST_PRINTF("PASS: Queue length equals TIKU_QUEUE_SIZE\n");
    } else {
        TEST_PRINTF("FAIL: Queue length = %d (expected %d)\n",
                     tiku_process_queue_length(), TIKU_QUEUE_SIZE);
    }

    if (tiku_process_queue_space() == 0) {
        TEST_PRINTF("PASS: Queue space is 0 when full\n");
    } else {
        TEST_PRINTF("FAIL: Queue space = %d (expected 0)\n",
                     tiku_process_queue_space());
    }

    /* Post should fail when queue is full */
    posted = tiku_process_post(&test_queue_proc,
                                TIKU_EVENT_CONTINUE, NULL);
    if (!posted) {
        TEST_PRINTF("PASS: Post returns 0 when queue is full\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Post succeeded on full queue\n");
    }

    /* --- Drain one event and verify counts update --- */
    tiku_process_run();

    if (tiku_process_queue_length() == TIKU_QUEUE_SIZE - 1) {
        TEST_PRINTF("PASS: Queue length decremented after one run\n");
    } else {
        TEST_PRINTF("FAIL: Queue length = %d (expected %d)\n",
                     tiku_process_queue_length(), TIKU_QUEUE_SIZE - 1);
    }

    if (tiku_process_queue_space() == 1) {
        TEST_PRINTF("PASS: Queue space is 1 after draining one\n");
    } else {
        TEST_PRINTF("FAIL: Queue space = %d (expected 1)\n",
                     tiku_process_queue_space());
    }

    if (!tiku_process_queue_full()) {
        TEST_PRINTF("PASS: Queue no longer full after drain\n");
    } else {
        TEST_PRINTF("FAIL: Queue still reports full\n");
    }

    /* --- tiku_process_is_running --- */
    if (tiku_process_is_running(&test_queue_proc)) {
        TEST_PRINTF("PASS: is_running returns 1 for active process\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: is_running returns 0 for active process\n");
    }

    /* Drain remaining events and exit the process */
    while (tiku_process_run()) {
        /* drain */
    }
    tiku_process_exit(&test_queue_proc);

    if (!tiku_process_is_running(&test_queue_proc)) {
        TEST_PRINTF("PASS: is_running returns 0 after exit\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: is_running returns 1 after exit\n");
    }

    tiku_atomic_exit();

    TEST_PRINTF("Queue query function test completed\n\n");
}

#endif /* TEST_PROCESS_QUEUE */

/*---------------------------------------------------------------------------*/
/* TEST 7: PROCESS LOCAL STORAGE                                             */
/*---------------------------------------------------------------------------*/

#if TEST_PROCESS_LOCAL

/** Local state for the TIKU_PROCESS_WITH_LOCAL test */
struct local_test_state {
    uint16_t counter;
    uint8_t  phase;
};

TIKU_PROCESS_WITH_LOCAL(test_local_proc, "test_local",
                        struct local_test_state);

TIKU_PROCESS_THREAD(test_local_proc, ev, data)
{
    struct local_test_state *s = TIKU_LOCAL(struct local_test_state);

    TIKU_PROCESS_BEGIN();

    /* Phase 1: initialize local storage */
    s->counter = 10;
    s->phase   = 1;
    TEST_PRINTF("Local proc: set counter=%d phase=%d\n",
                 s->counter, s->phase);
    TIKU_PROCESS_YIELD();

    /* Phase 2: verify state survived the yield and mutate */
    s->counter += 5;
    s->phase    = 2;
    TEST_PRINTF("Local proc: counter=%d phase=%d\n",
                 s->counter, s->phase);
    TIKU_PROCESS_YIELD();

    /* Phase 3: final check */
    s->counter += 1;
    s->phase    = 3;
    TEST_PRINTF("Local proc: counter=%d phase=%d\n",
                 s->counter, s->phase);

    TIKU_PROCESS_END();
}

/** Local state for the TIKU_PROCESS_TYPED test */
struct typed_test_state {
    uint16_t value;
};

TIKU_PROCESS_TYPED(test_typed_proc, "test_typed",
                   struct typed_test_state);

TIKU_PROCESS_THREAD(test_typed_proc, ev, data)
{
    struct typed_test_state *s = test_typed_proc_local();

    TIKU_PROCESS_BEGIN();

    s->value = 42;
    TEST_PRINTF("Typed proc: value=%d\n", s->value);
    TIKU_PROCESS_YIELD();

    s->value += 8;
    TEST_PRINTF("Typed proc: value=%d\n", s->value);

    TIKU_PROCESS_END();
}

void test_process_local_storage(void)
{
    struct local_test_state *ls;
    struct typed_test_state *ts;

    TEST_PRINTF("\n=== Test: Process Local Storage ===\n");

    tiku_process_init();

    /*---------------------------------------------------------------*/
    /* Part A: TIKU_PROCESS_WITH_LOCAL + TIKU_LOCAL accessor          */
    /*---------------------------------------------------------------*/

    tiku_process_start(&test_local_proc, NULL);

    /* Drain INIT -> executes phase 1, yields */
    while (tiku_process_run()) {
        /* drain */
    }

    ls = (struct local_test_state *)test_local_proc.local;

    if (ls->counter == 10 && ls->phase == 1) {
        TEST_PRINTF("PASS: Local state correct after phase 1\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected counter=10 phase=1, "
                     "got counter=%d phase=%d\n",
                     ls->counter, ls->phase);
    }

    /* Resume -> phase 2 */
    tiku_process_post(&test_local_proc, TIKU_EVENT_CONTINUE, NULL);
    while (tiku_process_run()) {
        /* drain */
    }

    if (ls->counter == 15 && ls->phase == 2) {
        TEST_PRINTF("PASS: Local state survived yield (phase 2)\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected counter=15 phase=2, "
                     "got counter=%d phase=%d\n",
                     ls->counter, ls->phase);
    }

    /* Resume -> phase 3, process ends */
    tiku_process_post(&test_local_proc, TIKU_EVENT_CONTINUE, NULL);
    while (tiku_process_run()) {
        /* drain */
    }

    if (ls->counter == 16 && ls->phase == 3) {
        TEST_PRINTF("PASS: Local state correct at exit (phase 3)\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected counter=16 phase=3, "
                     "got counter=%d phase=%d\n",
                     ls->counter, ls->phase);
    }

    if (!test_local_proc.is_running) {
        TEST_PRINTF("PASS: Process exited normally\n");
    } else {
        TEST_PRINTF("FAIL: Process still running\n");
    }

    /*---------------------------------------------------------------*/
    /* Part B: TIKU_PROCESS_TYPED accessor                           */
    /*---------------------------------------------------------------*/

    tiku_process_start(&test_typed_proc, NULL);

    /* Drain INIT -> sets value=42, yields */
    while (tiku_process_run()) {
        /* drain */
    }

    ts = test_typed_proc_local();

    if (ts->value == 42) {
        TEST_PRINTF("PASS: Typed accessor returns correct value\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected value=42, got %d\n", ts->value);
    }

    /* Resume -> value=50, process ends */
    tiku_process_post(&test_typed_proc, TIKU_EVENT_CONTINUE, NULL);
    while (tiku_process_run()) {
        /* drain */
    }

    if (ts->value == 50) {
        TEST_PRINTF("PASS: Typed state survived yield\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Expected value=50, got %d\n", ts->value);
    }

    if (!test_typed_proc.is_running) {
        TEST_PRINTF("PASS: Typed process exited normally\n");
    } else {
        TEST_PRINTF("FAIL: Typed process still running\n");
    }

    /*---------------------------------------------------------------*/
    /* Part C: Plain TIKU_PROCESS has local == NULL                   */
    /*---------------------------------------------------------------*/

    if (test_local_proc.local != NULL) {
        TEST_PRINTF("PASS: WITH_LOCAL process has non-NULL local\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: WITH_LOCAL process has NULL local\n");
    }

    TEST_PRINTF("Process local storage test completed\n\n");
}

#endif /* TEST_PROCESS_LOCAL */

/*---------------------------------------------------------------------------*/
/* TEST 8: BROADCAST EXIT SAFETY                                             */
/*---------------------------------------------------------------------------*/

#if TEST_PROCESS_BROADCAST_EXIT

/**
 * Verifies fix for broadcast list corruption: if a process exits while
 * the scheduler is iterating the process list during a broadcast, the
 * remaining processes must still receive the event.
 *
 * Setup: three processes [A] -> [B] -> [C].
 * B exits itself on receiving the broadcast event.
 * A and C must still receive the event.
 */

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

/*---------------------------------------------------------------------------*/
/* TEST 9: GRACEFUL EXIT VS FORCE EXIT                                       */
/*---------------------------------------------------------------------------*/

#if TEST_PROCESS_GRACEFUL_EXIT

/**
 * Verifies the TIKU_EVENT_EXIT / TIKU_EVENT_FORCE_EXIT split:
 *
 * Part A: TIKU_EVENT_EXIT is a polite request — the process thread
 *         can handle it, do cleanup, and decide when to exit.
 *
 * Part B: TIKU_EVENT_FORCE_EXIT unconditionally kills the process
 *         regardless of what the thread returns.
 */

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

/*---------------------------------------------------------------------------*/
/* TEST 10: CURRENT PROCESS CLEARED AFTER DISPATCH                           */
/*---------------------------------------------------------------------------*/

#if TEST_PROCESS_CURRENT_CLEARED

/**
 * Verifies that tiku_current_process (TIKU_THIS()) is NULL after
 * call_process returns, so code outside process context (main loop,
 * ISRs) does not see a stale pointer.
 */

static volatile unsigned int cc_inside_ok = 0;

TIKU_PROCESS(test_cc_proc, "cc_proc");

TIKU_PROCESS_THREAD(test_cc_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    /* Verify TIKU_THIS() is valid inside the thread */
    if (TIKU_THIS() == &test_cc_proc) {
        cc_inside_ok = 1;
    }

    TIKU_PROCESS_WAIT_EVENT();

    TIKU_PROCESS_END();
}

void test_process_current_cleared(void)
{
    TEST_PRINTF("\n=== Test: Current Process Cleared After Dispatch ===\n");

    cc_inside_ok = 0;

    tiku_process_init();
    tiku_process_start(&test_cc_proc, NULL);

    /* Drain INIT — thread runs, sets cc_inside_ok */
    while (tiku_process_run()) {
        /* drain */
    }

    /* Inside the thread, TIKU_THIS() should have been &test_cc_proc */
    if (cc_inside_ok == 1) {
        TEST_PRINTF("PASS: TIKU_THIS() valid inside process thread\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: TIKU_THIS() wrong inside process thread\n");
    }

    /* After dispatch returns, TIKU_THIS() must be NULL */
    if (TIKU_THIS() == NULL) {
        TEST_PRINTF("PASS: TIKU_THIS() is NULL after dispatch\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: TIKU_THIS() still points to %s\n",
                     TIKU_THIS()->name);
    }

    /* Clean up */
    tiku_process_exit(&test_cc_proc);
    TEST_PRINTF("Current process cleared test completed\n\n");
}

#endif /* TEST_PROCESS_CURRENT_CLEARED */

#endif /* PLATFORM_MSP430 */
