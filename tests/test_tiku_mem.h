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

#include "tiku.h"
#include "kernel/cpu/tiku_common.h"

void test_mem_create_and_stats(void);
void test_mem_basic_alloc(void);
void test_mem_alignment(void);
void test_mem_arena_full(void);
void test_mem_reset(void);
void test_mem_peak_tracking(void);
void test_mem_invalid_inputs(void);
void test_mem_secure_reset(void);
void test_mem_two_arenas(void);
void test_persist_init_zeroed(void);
void test_persist_register_and_count(void);
void test_persist_write_read(void);
void test_persist_read_small_buffer(void);
void test_persist_write_exceeds_capacity(void);
void test_persist_read_not_found(void);
void test_persist_delete(void);
void test_persist_full(void);
void test_persist_reboot_survival(void);
void test_persist_wear_check(void);
void test_persist_register_twice(void);
void test_mpu_init_defaults(void);
void test_mpu_unlock_lock(void);
void test_mpu_set_permissions(void);
void test_mpu_scoped_write(void);
void test_mpu_idempotent(void);

#endif /* TEST_TIKU_MEM_H_ */
