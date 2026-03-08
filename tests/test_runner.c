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
#include "tests/watchdog/test_watchdog.h"
#endif

#if TEST_CPUCLOCK
#include "tests/cpuclock/test_cpuclock.h"
#endif

#if TEST_PROCESS
#include "tests/process/test_process.h"
#endif

#if TEST_TIMER
#include "tests/timer/test_timer.h"
#endif

#if TEST_MEM || TEST_PERSIST || TEST_MPU || TEST_POOL || TEST_REGION
#include "tests/memory/test_tiku_mem.h"
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
static void test_run_mem(void);
static void test_run_mpu(void);
static void test_run_persist(void);
static void test_run_pool(void);
static void test_run_region(void);

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
    test_run_mem();
    test_run_mpu();
    test_run_persist();
    test_run_pool();
    test_run_region();

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

#if TEST_PROCESS_BROADCAST_EXIT
    test_process_broadcast_exit();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PROCESS_GRACEFUL_EXIT
    test_process_graceful_exit();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PROCESS_CURRENT_CLEARED
    test_process_current_cleared();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

    MAIN_PRINTF("Process/threading tests completed\n");
#endif
}

static void test_run_mem(void)
{
#if TEST_MEM
    MAIN_PRINTF("Running memory tests\n");

#if TEST_MEM_CREATE
    test_mem_create_and_stats();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MEM_ALLOC
    test_mem_basic_alloc();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MEM_ALIGNMENT
    test_mem_alignment();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MEM_FULL
    test_mem_arena_full();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MEM_RESET
    test_mem_reset();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MEM_PEAK
    test_mem_peak_tracking();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MEM_INVALID
    test_mem_invalid_inputs();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MEM_SECURE_RESET
    test_mem_secure_reset();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MEM_TWO_ARENAS
    test_mem_two_arenas();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

    MAIN_PRINTF("Memory tests completed\n");
#endif
}

static void test_run_mpu(void)
{
#if TEST_MPU
    MAIN_PRINTF("Running MPU tests\n");

#if TEST_MPU_INIT
    test_mpu_init_defaults();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MPU_UNLOCK_LOCK
    test_mpu_unlock_lock();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MPU_SET_PERM
    test_mpu_set_permissions();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MPU_SCOPED
    test_mpu_scoped_write();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MPU_IDEMPOTENT
    test_mpu_idempotent();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MPU_ALL_SEGMENTS
    test_mpu_all_segments();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MPU_PERM_FLAGS
    test_mpu_permission_flags();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MPU_REINIT
    test_mpu_reinit_restores();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MPU_UNLOCK_CUSTOM
    test_mpu_unlock_custom_base();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MPU_SCOPED_CUSTOM
    test_mpu_scoped_write_custom();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_MPU_VIOLATION
    test_mpu_violation_detect();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

    MAIN_PRINTF("MPU tests completed\n");
#endif
}

static void test_run_persist(void)
{
#if TEST_PERSIST
    MAIN_PRINTF("Running persistent store tests\n");

#if TEST_PERSIST_INIT
    test_persist_init_zeroed();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_REGISTER
    test_persist_register_and_count();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_WRITE_READ
    test_persist_write_read();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_SMALL_BUF
    test_persist_read_small_buffer();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_OVERFLOW
    test_persist_write_exceeds_capacity();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_NOT_FOUND
    test_persist_read_not_found();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_DELETE
    test_persist_delete();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_FULL
    test_persist_full();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_REBOOT
    test_persist_reboot_survival();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_POWERCYCLE
    test_persist_powercycle_survival();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_WEAR
    test_persist_wear_check();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_PERSIST_DUP_KEY
    test_persist_register_twice();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

    MAIN_PRINTF("Persistent store tests completed\n");
#endif
}

static void test_run_pool(void)
{
#if TEST_POOL
    MAIN_PRINTF("Running pool allocator tests\n");

#if TEST_POOL_CREATE
    test_pool_create_and_stats();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_ALLOC_FREE
    test_pool_basic_alloc_free();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_EXHAUSTION
    test_pool_exhaustion();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_FREE_RANGE
    test_pool_free_out_of_range();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_FREE_ALIGN
    test_pool_free_misaligned();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_REALLOC
    test_pool_alloc_free_realloc();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_PEAK
    test_pool_peak_tracking();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_RESET
    test_pool_reset();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_INVALID
    test_pool_invalid_inputs();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_TWO_POOLS
    test_pool_two_pools();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_BLOCK_ALIGN
    test_pool_block_size_alignment();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_STATS
    test_pool_stats_mapping();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_POISON
    test_pool_debug_poisoning();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_POOL_WITHIN_BUF
    test_pool_alloc_within_buffer();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

    MAIN_PRINTF("Pool allocator tests completed\n");
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

static void test_run_region(void)
{
#if TEST_REGION
    MAIN_PRINTF("Running region registry tests\n");

#if TEST_REGION_INIT
    test_region_init_valid();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_INIT_INVALID
    test_region_init_invalid();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_CONTAINS
    test_region_contains_basic();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_WRONG_TYPE
    test_region_contains_wrong_type();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_BOUNDARY
    test_region_contains_boundary();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_OVERFLOW
    test_region_contains_overflow();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_CLAIM
    test_region_claim_unclaim();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_CLAIM_OVERLAP
    test_region_claim_overlap();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_CLAIM_UNKNOWN
    test_region_claim_unknown();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_CLAIM_FULL
    test_region_claim_full();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_GET_TYPE
    test_region_get_type_found();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

#if TEST_REGION_NOT_FOUND
    test_region_get_type_not_found();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif

    MAIN_PRINTF("Region registry tests completed\n");
#endif
}
