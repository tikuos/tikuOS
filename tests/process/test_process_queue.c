/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_process_queue.c - Queue query functions test
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

#endif /* PLATFORM_MSP430 */
