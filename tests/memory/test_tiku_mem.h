/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_tiku_mem.h - Memory subsystem test interface
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

#ifndef TEST_TIKU_MEM_H_
#define TEST_TIKU_MEM_H_

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kernel/memory/tiku_mem.h"

/*---------------------------------------------------------------------------*/
/* TEST HARNESS                                                              */
/*---------------------------------------------------------------------------*/

/*
 * On MSP430 target, bare printf() has no UART backend — route through
 * TIKU_PRINTF.  In host-mode builds, plain printf() works fine.
 */
#ifdef PLATFORM_MSP430
#include "tiku.h"
#define TEST_PRINT(...) TIKU_PRINTF(__VA_ARGS__)
#else
#define TEST_PRINT(...) printf(__VA_ARGS__)
#endif

extern int tests_run;
extern int tests_passed;
extern int tests_failed;

#define TEST_ASSERT(cond, msg)                                              \
    do {                                                                    \
        tests_run++;                                                        \
        if (cond) {                                                         \
            tests_passed++;                                                 \
            TEST_PRINT("  PASS: %s\n", msg);                                \
        } else {                                                            \
            tests_failed++;                                                 \
            TEST_PRINT("  FAIL: %s\n", msg);                                \
        }                                                                   \
    } while (0)

/** Round @p x up to the platform alignment — mirrors the kernel's align_up */
#define TEST_ALIGN_UP(x) \
    (((x) + (TIKU_MEM_ARCH_ALIGNMENT - 1U)) & ~(TIKU_MEM_ARCH_ALIGNMENT - 1U))

/*---------------------------------------------------------------------------*/
/* SHARED TEST POOLS                                                         */
/*---------------------------------------------------------------------------*/

/*
 * Static pools for region-aware testing. Arena tests allocate from the
 * SRAM pool; persist tests use the NVM pool. On MSP430, test_nvm_pool
 * is placed in FRAM via the .persistent section attribute.
 */

/** Size of the shared SRAM test pool in bytes */
#ifndef TEST_SRAM_POOL_SIZE
#define TEST_SRAM_POOL_SIZE  256
#endif

/** Size of the shared NVM test pool in bytes */
#ifndef TEST_NVM_POOL_SIZE
#define TEST_NVM_POOL_SIZE   256
#endif

extern uint8_t test_sram_pool[];
extern uint8_t test_nvm_pool[];

/**
 * Reinitialize the region registry with the platform table.
 * Clears the claimed regions array. Call before each test that
 * creates arenas or claims regions to start with a clean slate.
 */
void test_region_reinit(void);

/*---------------------------------------------------------------------------*/
/* ARENA ALLOCATOR TESTS                                                     */
/*---------------------------------------------------------------------------*/

void test_mem_create_and_stats(void);
void test_mem_basic_alloc(void);
void test_mem_alignment(void);
void test_mem_arena_full(void);
void test_mem_reset(void);
void test_mem_peak_tracking(void);
void test_mem_invalid_inputs(void);
void test_mem_secure_reset(void);
void test_mem_two_arenas(void);

/*---------------------------------------------------------------------------*/
/* PERSISTENT STORE TESTS                                                    */
/*---------------------------------------------------------------------------*/

/** Reboot survival test phase: write data and trigger reset */
#define TEST_PERSIST_REBOOT_PHASE_WRITE   0

/** Reboot survival test phase: verify data after reboot */
#define TEST_PERSIST_REBOOT_PHASE_VERIFY  1

/** Power-cycle survival test phase: write data and wait for power removal */
#define TEST_PERSIST_PWRCYCLE_PHASE_WRITE   0

/** Power-cycle survival test phase: verify data after power restore */
#define TEST_PERSIST_PWRCYCLE_PHASE_VERIFY  1

void test_persist_init_zeroed(void);
void test_persist_register_and_count(void);
void test_persist_write_read(void);
void test_persist_read_small_buffer(void);
void test_persist_write_exceeds_capacity(void);
void test_persist_read_not_found(void);
void test_persist_delete(void);
void test_persist_full(void);
void test_persist_reboot_survival(void);
void test_persist_powercycle_survival(void);
void test_persist_wear_check(void);
void test_persist_register_twice(void);

/*---------------------------------------------------------------------------*/
/* MPU TESTS                                                                 */
/*---------------------------------------------------------------------------*/

void test_mpu_init_defaults(void);
void test_mpu_unlock_lock(void);
void test_mpu_set_permissions(void);
void test_mpu_scoped_write(void);
void test_mpu_idempotent(void);
void test_mpu_all_segments(void);
void test_mpu_permission_flags(void);
void test_mpu_reinit_restores(void);
void test_mpu_unlock_custom_base(void);
void test_mpu_scoped_write_custom(void);
void test_mpu_violation_detect(void);

/*---------------------------------------------------------------------------*/
/* POOL ALLOCATOR TESTS                                                      */
/*---------------------------------------------------------------------------*/

void test_pool_create_and_stats(void);
void test_pool_basic_alloc_free(void);
void test_pool_exhaustion(void);
void test_pool_free_out_of_range(void);
void test_pool_free_misaligned(void);
void test_pool_alloc_free_realloc(void);
void test_pool_peak_tracking(void);
void test_pool_reset(void);
void test_pool_invalid_inputs(void);
void test_pool_two_pools(void);
void test_pool_block_size_alignment(void);
void test_pool_stats_mapping(void);
void test_pool_debug_poisoning(void);
void test_pool_alloc_within_buffer(void);

/*---------------------------------------------------------------------------*/
/* REGION REGISTRY TESTS                                                     */
/*---------------------------------------------------------------------------*/

void test_region_init_valid(void);
void test_region_init_invalid(void);
void test_region_contains_basic(void);
void test_region_contains_wrong_type(void);
void test_region_contains_boundary(void);
void test_region_contains_overflow(void);
void test_region_claim_unclaim(void);
void test_region_claim_overlap(void);
void test_region_claim_unknown(void);
void test_region_claim_full(void);
void test_region_get_type_found(void);
void test_region_get_type_not_found(void);

/*---------------------------------------------------------------------------*/
/* HOST-ONLY TEST HELPERS                                                    */
/*---------------------------------------------------------------------------*/

#ifndef PLATFORM_MSP430
/**
 * Simulate a write attempt to a given MPU segment.
 * Sets the violation flag if the segment lacks write permission.
 */
void test_mpu_trigger_seg_violation(tiku_mpu_seg_t seg);
#endif

#endif /* TEST_TIKU_MEM_H_ */
