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
/* SHARED TEST POOLS                                                         */
/*---------------------------------------------------------------------------*/

/*
 * Static pools for region-aware testing. Arena tests use test_sram_pool
 * (natural SRAM / BSS on both host and target). Persist tests use
 * test_nvm_pool (placed in FRAM on MSP430, plain memory on host).
 */

uint8_t test_sram_pool[TEST_SRAM_POOL_SIZE];

#ifdef PLATFORM_MSP430
uint8_t __attribute__((section(".persistent")))
    test_nvm_pool[TEST_NVM_POOL_SIZE] = {0};
#else
uint8_t test_nvm_pool[TEST_NVM_POOL_SIZE];
#endif

/*---------------------------------------------------------------------------*/
/* REGION REINIT HELPER                                                      */
/*---------------------------------------------------------------------------*/

/*
 * Reinitialize the region registry from the platform table, clearing
 * the claimed-regions array. Called at the start of each test that
 * creates arenas (which call tiku_region_claim internally) to ensure
 * a clean slate.
 */
void test_region_reinit(void)
{
    tiku_mem_arch_size_t count;

    tiku_region_init(tiku_region_arch_get_table(&count), count);
}

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

/*---------------------------------------------------------------------------*/
/* REGION HAL STUB                                                           */
/*---------------------------------------------------------------------------*/

/*
 * Host-mode region table — maps test_sram_pool as SRAM and
 * test_nvm_pool as NVM. On real hardware, the platform arch layer
 * returns the actual memory map instead.
 */
static const tiku_mem_region_t test_region_table[] = {
    { test_sram_pool, sizeof(test_sram_pool), TIKU_MEM_REGION_SRAM },
    { test_nvm_pool,  sizeof(test_nvm_pool),  TIKU_MEM_REGION_NVM  },
};

const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count)
{
    *count = sizeof(test_region_table) / sizeof(test_region_table[0]);
    return test_region_table;
}

/*---------------------------------------------------------------------------*/
/* MPU HAL STUBS                                                             */
/*---------------------------------------------------------------------------*/

/*
 * MPU HAL stubs — plain variables mimic MPU registers on the host.
 * The kernel MPU layer (tiku_mpu.c) calls these HAL functions instead
 * of touching hardware registers directly, so the test can inspect
 * the resulting state without real MPU hardware.
 */
static uint16_t stub_mpuctl0;
static uint16_t stub_mpusam;
static uint16_t stub_mpusegb1;
static uint16_t stub_mpusegb2;

void tiku_mpu_arch_init_segments(void)
{
    stub_mpusegb1 = 0x0800;  /* 0x8000 >> 4 */
    stub_mpusegb2 = 0x0C00;  /* 0xC000 >> 4 */
}

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

static uint16_t stub_mpuctl1;

void tiku_mpu_arch_set_default_protection(void)
{
    tiku_mpu_arch_set_sam(0x0555);
}

void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
{
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07 << shift;
    uint16_t sam   = tiku_mpu_arch_get_sam();

    sam = (sam & ~mask) | (((uint16_t)perm & 0x07) << shift);
    tiku_mpu_arch_set_sam(sam);
}

uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    uint16_t saved = tiku_mpu_arch_get_sam();

    tiku_mpu_arch_set_sam(saved | 0x0222);

    return saved;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state)
{
    tiku_mpu_arch_set_sam(saved_state);
}

uint16_t tiku_mpu_arch_get_violation_flags(void)  { return stub_mpuctl1; }
void tiku_mpu_arch_clear_violation_flags(void)    { stub_mpuctl1 = 0; }
void tiku_mpu_arch_enable_violation_nmi(void)
{
    stub_mpuctl0 |= 0x0010;  /* MPUSEGIE bit */
}

/*
 * Host-only: simulate a write attempt to a given segment.
 * Checks the current SAM permissions — if the segment lacks the write
 * bit, the corresponding violation flag is set in stub_mpuctl1,
 * mimicking what MSP430 hardware does on a real violation.
 */
void test_mpu_trigger_seg_violation(tiku_mpu_seg_t seg)
{
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t perm  = (stub_mpusam >> shift) & 0x07;

    if (!(perm & 0x02)) {  /* Write bit not set — violation */
        stub_mpuctl1 |= (1U << (uint16_t)seg);
    }
}

/*---------------------------------------------------------------------------*/
/* HOST-ONLY MAIN                                                            */
/*---------------------------------------------------------------------------*/

int main(void)
{
    /* Initialize memory subsystem (region registry + MPU stubs) */
    tiku_mem_init();

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
    test_mpu_all_segments();
    test_mpu_permission_flags();
    test_mpu_reinit_restores();
    test_mpu_unlock_custom_base();
    test_mpu_scoped_write_custom();
    test_mpu_violation_detect();

    /* Pool allocator tests */
    test_pool_create_and_stats();
    test_pool_basic_alloc_free();
    test_pool_exhaustion();
    test_pool_free_out_of_range();
    test_pool_free_misaligned();
    test_pool_alloc_free_realloc();
    test_pool_peak_tracking();
    test_pool_reset();
    test_pool_invalid_inputs();
    test_pool_two_pools();
    test_pool_block_size_alignment();
    test_pool_stats_mapping();
    test_pool_debug_poisoning();
    test_pool_alloc_within_buffer();

    /* Region registry tests */
    test_region_init_valid();
    test_region_init_invalid();
    test_region_contains_basic();
    test_region_contains_wrong_type();
    test_region_contains_boundary();
    test_region_contains_overflow();
    test_region_claim_unclaim();
    test_region_claim_overlap();
    test_region_claim_unknown();
    test_region_claim_full();
    test_region_get_type_found();
    test_region_get_type_not_found();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

#endif /* !PLATFORM_MSP430 */
