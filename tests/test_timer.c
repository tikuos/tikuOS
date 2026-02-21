/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_timer.c - Timer subsystem tests
 *
 * Tests the software timer (tiku_timer) and hardware timer (tiku_htimer):
 * 1. Event timer - process waits for TIKU_EVENT_TIMER
 * 2. Callback timer - function fires on expiration
 * 3. Periodic timer - drift-free reset operation
 * 4. Timer stop - set and cancel before expiration
 * 5. Hardware timer basic - one-shot htimer
 * 6. Hardware timer periodic - self-rescheduling from ISR callback
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

/*---------------------------------------------------------------------------*/
/* PRIVATE CONSTANTS                                                         */
/*---------------------------------------------------------------------------*/

#define TEST_TIMER_INTERVAL     (TIKU_CLOCK_SECOND)      /* 1 second */
#define TEST_TIMER_SHORT        (TIKU_CLOCK_SECOND / 4)  /* 250 ms */
#define TEST_TIMER_PERIODIC_CNT 3
#define TEST_TIMER_DRAIN_MAX    500  /* max scheduler loops */

#define TEST_HTIMER_PERIOD      (TIKU_HTIMER_SECOND / 10)  /* 100 ms */
#define TEST_HTIMER_REPEAT_CNT  5

/*---------------------------------------------------------------------------*/
/* TEST 1: EVENT TIMER                                                       */
/*---------------------------------------------------------------------------*/

#if TEST_TIMER_EVENT

static volatile unsigned int event_timer_fired = 0;
static struct tiku_timer event_tmr;

TIKU_PROCESS(test_event_timer_proc, "test_evt_tmr");

TIKU_PROCESS_THREAD(test_event_timer_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    /* Set a 1-second event timer */
    tiku_timer_set_event(&event_tmr, TEST_TIMER_INTERVAL);
    TEST_PRINTF("Event timer: set for %u ticks\n",
                (unsigned int)TEST_TIMER_INTERVAL);

    /* Wait for the timer event */
    TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

    event_timer_fired = 1;
    TEST_PRINTF("Event timer: received TIKU_EVENT_TIMER\n");

    TIKU_PROCESS_END();
}

void test_timer_event(void)
{
    unsigned int loops;

    TEST_PRINTF("\n=== Test: Event Timer ===\n");

    event_timer_fired = 0;

    tiku_process_init();
    tiku_timer_init();
    tiku_clock_init();
    tiku_process_start(&test_event_timer_proc, NULL);

    /* Drain INIT and let the process set its timer */
    while (tiku_process_run()) {
        /* drain */
    }

    TEST_PRINTF("Event timer: waiting for expiration...\n");

    /* Poll until timer fires or we time out */
    for (loops = 0; loops < TEST_TIMER_DRAIN_MAX; loops++) {
        tiku_clock_wait(1);
        tiku_timer_request_poll();
        while (tiku_process_run()) {
            /* drain */
        }
        if (event_timer_fired) {
            break;
        }
    }

    if (event_timer_fired) {
        TEST_PRINTF("PASS: Event timer fired after %u poll loops\n", loops);
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Event timer did not fire within %u loops\n",
                     TEST_TIMER_DRAIN_MAX);
    }

    /* Verify timer is no longer active */
    if (tiku_timer_expired(&event_tmr)) {
        TEST_PRINTF("PASS: Timer reports expired\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Timer still reports active\n");
    }

    TEST_PRINTF("Event timer test completed\n\n");
}

#endif /* TEST_TIMER_EVENT */

/*---------------------------------------------------------------------------*/
/* TEST 2: CALLBACK TIMER                                                    */
/*---------------------------------------------------------------------------*/

#if TEST_TIMER_CALLBACK

static volatile unsigned int callback_count = 0;

static void test_callback_func(void *ptr)
{
    callback_count++;
    TEST_PRINTF("Callback timer: fired (count=%u)\n", callback_count);
}

static struct tiku_timer callback_tmr;

TIKU_PROCESS(test_callback_timer_proc, "test_cb_tmr");

TIKU_PROCESS_THREAD(test_callback_timer_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    /* Set a callback timer for 250ms */
    tiku_timer_set_callback(&callback_tmr, TEST_TIMER_SHORT,
                            test_callback_func, NULL);
    TEST_PRINTF("Callback timer: set for %u ticks\n",
                (unsigned int)TEST_TIMER_SHORT);

    /* Yield — callback will fire from the timer process */
    TIKU_PROCESS_WAIT_EVENT();

    TIKU_PROCESS_END();
}

void test_timer_callback(void)
{
    unsigned int loops;

    TEST_PRINTF("\n=== Test: Callback Timer ===\n");

    callback_count = 0;

    tiku_process_init();
    tiku_timer_init();
    tiku_clock_init();
    tiku_process_start(&test_callback_timer_proc, NULL);

    /* Drain INIT — process sets its callback timer */
    while (tiku_process_run()) {
        /* drain */
    }

    TEST_PRINTF("Callback timer: waiting for expiration...\n");

    /* Poll until callback fires */
    for (loops = 0; loops < TEST_TIMER_DRAIN_MAX; loops++) {
        tiku_clock_wait(1);
        tiku_timer_request_poll();
        while (tiku_process_run()) {
            /* drain */
        }
        if (callback_count > 0) {
            break;
        }
    }

    if (callback_count == 1) {
        TEST_PRINTF("PASS: Callback fired exactly once\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Callback count = %u (expected 1)\n",
                     callback_count);
    }

    tiku_process_exit(&test_callback_timer_proc);
    TEST_PRINTF("Callback timer test completed\n\n");
}

#endif /* TEST_TIMER_CALLBACK */

/*---------------------------------------------------------------------------*/
/* TEST 3: PERIODIC TIMER (DRIFT-FREE RESET)                                */
/*---------------------------------------------------------------------------*/

#if TEST_TIMER_PERIODIC

static volatile unsigned int periodic_count = 0;

static struct tiku_timer periodic_tmr;

static void test_periodic_func(void *ptr)
{
    periodic_count++;
    TEST_PRINTF("Periodic timer: tick %u/%u\n",
                periodic_count, TEST_TIMER_PERIODIC_CNT);

    if (periodic_count < TEST_TIMER_PERIODIC_CNT) {
        /* Drift-free reschedule */
        tiku_timer_reset(&periodic_tmr);
    }
}

TIKU_PROCESS(test_periodic_timer_proc, "test_per_tmr");

TIKU_PROCESS_THREAD(test_periodic_timer_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_timer_set_callback(&periodic_tmr, TEST_TIMER_SHORT,
                            test_periodic_func, NULL);
    TEST_PRINTF("Periodic timer: set for %u ticks, %u repeats\n",
                (unsigned int)TEST_TIMER_SHORT, TEST_TIMER_PERIODIC_CNT);

    /* Keep alive while the periodic timer runs */
    while (periodic_count < TEST_TIMER_PERIODIC_CNT) {
        TIKU_PROCESS_WAIT_EVENT();
    }

    TIKU_PROCESS_END();
}

void test_timer_periodic(void)
{
    unsigned int loops;

    TEST_PRINTF("\n=== Test: Periodic Timer ===\n");

    periodic_count = 0;

    tiku_process_init();
    tiku_timer_init();
    tiku_clock_init();
    tiku_process_start(&test_periodic_timer_proc, NULL);

    /* Drain INIT */
    while (tiku_process_run()) {
        /* drain */
    }

    TEST_PRINTF("Periodic timer: waiting for %u callbacks...\n",
                TEST_TIMER_PERIODIC_CNT);

    for (loops = 0; loops < TEST_TIMER_DRAIN_MAX * TEST_TIMER_PERIODIC_CNT;
         loops++) {
        tiku_clock_wait(1);
        tiku_timer_request_poll();
        while (tiku_process_run()) {
            /* drain */
        }
        if (periodic_count >= TEST_TIMER_PERIODIC_CNT) {
            break;
        }
    }

    if (periodic_count == TEST_TIMER_PERIODIC_CNT) {
        TEST_PRINTF("PASS: Periodic timer fired %u times\n",
                     TEST_TIMER_PERIODIC_CNT);
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Periodic count = %u (expected %u)\n",
                     periodic_count, TEST_TIMER_PERIODIC_CNT);
    }

    /* Verify timer is no longer active (stopped after last tick) */
    if (tiku_timer_expired(&periodic_tmr)) {
        TEST_PRINTF("PASS: Timer stopped after final tick\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Timer still active after final tick\n");
    }

    tiku_process_exit(&test_periodic_timer_proc);
    TEST_PRINTF("Periodic timer test completed\n\n");
}

#endif /* TEST_TIMER_PERIODIC */

/*---------------------------------------------------------------------------*/
/* TEST 4: TIMER STOP                                                        */
/*---------------------------------------------------------------------------*/

#if TEST_TIMER_STOP

static volatile unsigned int stop_callback_fired = 0;
static struct tiku_timer stop_tmr;

static void test_stop_func(void *ptr)
{
    stop_callback_fired = 1;
    TEST_PRINTF("Stop timer: callback fired (unexpected!)\n");
}

void test_timer_stop(void)
{
    unsigned int loops;

    TEST_PRINTF("\n=== Test: Timer Stop ===\n");

    stop_callback_fired = 0;

    tiku_process_init();
    tiku_timer_init();
    tiku_clock_init();

    /* Set a 1-second callback timer */
    tiku_timer_set_callback(&stop_tmr, TEST_TIMER_INTERVAL,
                            test_stop_func, NULL);
    TEST_PRINTF("Stop timer: set for %u ticks\n",
                (unsigned int)TEST_TIMER_INTERVAL);

    /* Verify timer is active */
    if (!tiku_timer_expired(&stop_tmr)) {
        TEST_PRINTF("PASS: Timer is active after set\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Timer reports expired right after set\n");
    }

    /* Stop it before it fires */
    tiku_timer_stop(&stop_tmr);
    TEST_PRINTF("Stop timer: stopped\n");

    /* Verify timer reports expired (removed from active list) */
    if (tiku_timer_expired(&stop_tmr)) {
        TEST_PRINTF("PASS: Timer reports expired after stop\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Timer still active after stop\n");
    }

    /* Run the scheduler past the original expiration to confirm */
    for (loops = 0; loops < TEST_TIMER_DRAIN_MAX; loops++) {
        tiku_clock_wait(1);
        tiku_timer_request_poll();
        while (tiku_process_run()) {
            /* drain */
        }
    }

    if (stop_callback_fired == 0) {
        TEST_PRINTF("PASS: Callback did not fire after stop\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: Callback fired despite stop\n");
    }

    TEST_PRINTF("Timer stop test completed\n\n");
}

#endif /* TEST_TIMER_STOP */

/*---------------------------------------------------------------------------*/
/* TEST 5: HARDWARE TIMER BASIC (ONE-SHOT)                                   */
/*---------------------------------------------------------------------------*/

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

/*---------------------------------------------------------------------------*/
/* TEST 6: HARDWARE TIMER PERIODIC (SELF-RESCHEDULE)                         */
/*---------------------------------------------------------------------------*/

#if TEST_HTIMER_PERIODIC

static volatile unsigned int htimer_periodic_count = 0;
static struct tiku_htimer periodic_ht;

static void test_htimer_periodic_cb(struct tiku_htimer *t, void *ptr)
{
    htimer_periodic_count++;
    TEST_PRINTF("HTimer periodic: tick %u/%u\n",
                htimer_periodic_count, TEST_HTIMER_REPEAT_CNT);

    if (htimer_periodic_count < TEST_HTIMER_REPEAT_CNT) {
        /* Drift-free reschedule from the scheduled time */
        tiku_htimer_set(t, TIKU_HTIMER_TIME(t) + TEST_HTIMER_PERIOD,
                        test_htimer_periodic_cb, NULL);
    }
}

void test_htimer_periodic(void)
{
    int ret;
    tiku_htimer_clock_t target;
    unsigned int wait_loops;

    TEST_PRINTF("\n=== Test: Hardware Timer Periodic ===\n");

    htimer_periodic_count = 0;

    tiku_htimer_init();

    /* Schedule first tick 100ms from now */
    target = TIKU_HTIMER_NOW() + TEST_HTIMER_PERIOD;
    ret = tiku_htimer_set(&periodic_ht, target,
                          test_htimer_periodic_cb, NULL);

    if (ret == TIKU_HTIMER_OK) {
        TEST_PRINTF("HTimer periodic: started, expecting %u ticks\n",
                     TEST_HTIMER_REPEAT_CNT);
    } else {
        TEST_PRINTF("FAIL: tiku_htimer_set returned %d\n", ret);
        return;
    }

    /* Wait for all periodic ticks (busy-wait with timeout) */
    for (wait_loops = 0; wait_loops < 500000; wait_loops++) {
        if (htimer_periodic_count >= TEST_HTIMER_REPEAT_CNT) {
            break;
        }
        __no_operation();
    }

    if (htimer_periodic_count == TEST_HTIMER_REPEAT_CNT) {
        TEST_PRINTF("PASS: HTimer periodic completed %u ticks\n",
                     TEST_HTIMER_REPEAT_CNT);
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: HTimer periodic count = %u (expected %u)\n",
                     htimer_periodic_count, TEST_HTIMER_REPEAT_CNT);
    }

    /* After final tick, no reschedule, so nothing pending */
    if (!tiku_htimer_is_scheduled()) {
        TEST_PRINTF("PASS: No htimer scheduled after final tick\n");
        tiku_common_led1_toggle();
    } else {
        TEST_PRINTF("FAIL: HTimer still scheduled after final tick\n");
        tiku_htimer_cancel();
    }

    TEST_PRINTF("Hardware timer periodic test completed\n\n");
}

#endif /* TEST_HTIMER_PERIODIC */

#endif /* PLATFORM_MSP430 */
