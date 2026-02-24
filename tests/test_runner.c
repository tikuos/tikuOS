/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_runner.c - Test runner implementation
 *
 * Consolidates all test dispatching logic. Individual tests are enabled
 * via TEST_* flags in tiku.h.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"
#include "kernel/cpu/tiku_common.h"

#if TEST_WATCHDOG
#include "tests/test_watchdog.h"
#endif

#if TEST_CPUCLOCK
#include "tests/test_cpuclock.h"
#endif

#if TEST_PROCESS
#include "tests/test_process.h"
#endif

#if TEST_TIMER
#include "tests/test_timer.h"
#endif

/*---------------------------------------------------------------------------*/
/* PRIVATE CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

#define TEST_DELAY_MS     1000    /* Delay between tests in milliseconds */

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTION PROTOTYPES                                              */
/*---------------------------------------------------------------------------*/

static void test_run_cpuclock(void);
static void test_run_watchdog(void);
static void test_run_process(void);
static void test_run_timer(void);

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                         */
/*---------------------------------------------------------------------------*/

void test_run_all(void)
{
    MAIN_PRINTF("=== TikuOS Test Suite ===\n");

    /* Pre-interrupt tests */
    test_run_cpuclock();
    test_run_watchdog();
    test_run_process();

    /* Enable global interrupts (needed for timer ISRs) */
    MAIN_PRINTF("Enabling global interrupts\n");
    __enable_interrupt();

    /* Post-interrupt tests */
    test_run_timer();

    MAIN_PRINTF("=== Test suite completed ===\n");
}

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTIONS                                                        */
/*---------------------------------------------------------------------------*/

static void test_run_cpuclock(void)
{
#if TEST_CPUCLOCK
    MAIN_PRINTF("Running CPU clock test\n");
    test_cpuclock_basic();
    MAIN_PRINTF("CPU clock test completed\n");
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif
}

static void test_run_watchdog(void)
{
#if TEST_WATCHDOG
    MAIN_PRINTF("Running watchdog tests\n");

#if TEST_WDT_BASIC
    MAIN_PRINTF("Running basic watchdog test\n");
    test_watchdog_basic();
    MAIN_PRINTF("Basic watchdog test completed\n");
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_WDT_PAUSE_RESUME
    MAIN_PRINTF("Running watchdog pause/resume test\n");
    test_watchdog_pause_resume();
    MAIN_PRINTF("Pause/resume test completed\n");
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_WDT_INTERVAL
    MAIN_PRINTF("Running watchdog interval timer test\n");
    test_watchdog_interval_timer();
    MAIN_PRINTF("Interval timer test completed\n");
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_WDT_TIMEOUT
    MAIN_PRINTF("WARNING: Starting watchdog timeout test"
                " - system will reset!\n");
    test_watchdog_timeout();
    /* Device resets - code won't reach here */
#endif

    MAIN_PRINTF("Watchdog tests completed\n");
#endif
}

static void test_run_process(void)
{
#if TEST_PROCESS
    MAIN_PRINTF("Running process/threading tests\n");

#if TEST_PROCESS_LIFECYCLE
    test_process_lifecycle();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PROCESS_EVENTS
    test_process_events();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PROCESS_YIELD
    test_process_yield();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PROCESS_BROADCAST
    test_process_broadcast();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PROCESS_POLL
    test_process_poll();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PROCESS_QUEUE
    test_process_queue_query();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PROCESS_LOCAL
    test_process_local_storage();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

    MAIN_PRINTF("Process/threading tests completed\n");
#endif
}

static void test_run_timer(void)
{
#if TEST_TIMER
    MAIN_PRINTF("Running timer subsystem tests\n");

#if TEST_TIMER_EVENT
    test_timer_event();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_TIMER_CALLBACK
    test_timer_callback();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_TIMER_PERIODIC
    test_timer_periodic();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_TIMER_STOP
    test_timer_stop();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_HTIMER_BASIC
    test_htimer_basic();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_HTIMER_PERIODIC
    test_htimer_periodic();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

    MAIN_PRINTF("Timer subsystem tests completed\n");
#endif
}
