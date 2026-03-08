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

#endif /* TEST_TIKU_MEM_H_ */
