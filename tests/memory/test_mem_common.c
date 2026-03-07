/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_mem_common.c - Shared test infrastructure and host-mode stubs
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

#include "test_tiku_mem.h"

/*---------------------------------------------------------------------------*/
/* TEST HARNESS COUNTERS                                                     */
/*---------------------------------------------------------------------------*/

int tests_run    = 0;
int tests_passed = 0;
int tests_failed = 0;

/*---------------------------------------------------------------------------*/
/* HOST-MODE ARCH STUBS                                                      */
/*---------------------------------------------------------------------------*/

/*
 * When compiling on a host (no PLATFORM_MSP430), the HAL provides
 * fallback type/alignment defaults but no function bodies. Supply
 * minimal portable implementations so the test stays single-file
 * compilable without the MSP430 arch sources.
 */
#ifndef PLATFORM_MSP430

void tiku_mem_arch_init(void)
{
    /* No-op on host. */
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)buf;
    tiku_mem_arch_size_t i;

    for (i = 0; i < len; i++) {
        p[i] = 0;
    }
}

void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len)
{
    memcpy(dst, src, len);
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len)
{
    memcpy(dst, src, len);
}

/*
 * MPU HAL stubs — plain variables mimic MPU registers on the host.
 * The kernel MPU layer (tiku_mpu.c) calls these HAL functions instead
 * of touching hardware registers directly, so the test can inspect
 * the resulting state without real MPU hardware.
 */
static uint16_t stub_mpuctl0;
static uint16_t stub_mpusam;

uint16_t tiku_mpu_arch_get_sam(void) { return stub_mpusam; }
void tiku_mpu_arch_set_sam(uint16_t sam)
{
    stub_mpuctl0 = 0xA500;       /* unlock (password) */
    stub_mpusam  = sam;
    stub_mpuctl0 = 0xA500 | 0x0001; /* password | enable */
}
uint16_t tiku_mpu_arch_get_ctl(void) { return stub_mpuctl0; }
void tiku_mpu_arch_disable_irq(void) { /* no-op on host */ }
void tiku_mpu_arch_enable_irq(void)  { /* no-op on host */ }

/*---------------------------------------------------------------------------*/
/* HOST-ONLY MAIN                                                            */
/*---------------------------------------------------------------------------*/

int main(void)
{
    printf("=== TikuOS Memory Module Tests ===\n");

    /* Arena allocator tests */
    test_mem_create_and_stats();
    test_mem_basic_alloc();
    test_mem_alignment();
    test_mem_arena_full();
    test_mem_reset();
    test_mem_peak_tracking();
    test_mem_invalid_inputs();
    test_mem_secure_reset();
    test_mem_two_arenas();

    /* Persistent FRAM key-value store tests */
    test_persist_init_zeroed();
    test_persist_register_and_count();
    test_persist_write_read();
    test_persist_read_small_buffer();
    test_persist_write_exceeds_capacity();
    test_persist_read_not_found();
    test_persist_delete();
    test_persist_full();
    test_persist_reboot_survival();
    test_persist_wear_check();
    test_persist_register_twice();

    /* MPU write-protection tests */
    test_mpu_init_defaults();
    test_mpu_unlock_lock();
    test_mpu_set_permissions();
    test_mpu_scoped_write();
    test_mpu_idempotent();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

#endif /* !PLATFORM_MSP430 */
