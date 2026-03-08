/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_mem_region.c - Memory region registry tests
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
/* TEST: REGION INIT WITH VALID TABLE                                        */
/*---------------------------------------------------------------------------*/

void test_region_init_valid(void)
{
    tiku_mem_arch_size_t count;
    const tiku_mem_region_t *table;
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Region Init Valid ---\n");

    table = tiku_region_arch_get_table(&count);
    err = tiku_region_init(table, count);
    TEST_ASSERT(err == TIKU_MEM_OK, "init with valid table succeeds");
    TEST_ASSERT(count >= 2, "table has at least SRAM and NVM regions");
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION INIT WITH INVALID ARGUMENTS                                  */
/*---------------------------------------------------------------------------*/

void test_region_init_invalid(void)
{
    tiku_mem_arch_size_t count;
    const tiku_mem_region_t *table;
    tiku_mem_err_t err;

    /*
     * Overlapping region table — two SRAM regions that share addresses.
     * tiku_region_init must detect the overlap and reject the table.
     */
    const tiku_mem_region_t overlap_table[] = {
        { test_sram_pool,      128, TIKU_MEM_REGION_SRAM },
        { test_sram_pool + 64, 128, TIKU_MEM_REGION_SRAM },
    };

    TEST_PRINT("\n--- Test: Region Init Invalid ---\n");

    /* NULL table */
    err = tiku_region_init(NULL, 1);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "NULL table rejected");

    /* Zero count */
    table = tiku_region_arch_get_table(&count);
    err = tiku_region_init(table, 0);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "zero count rejected");

    /* Overlapping regions */
    err = tiku_region_init(overlap_table, 2);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "overlapping regions rejected");

    /* Restore valid state for subsequent tests */
    test_region_reinit();
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION CONTAINS BASIC CHECK                                         */
/*---------------------------------------------------------------------------*/

void test_region_contains_basic(void)
{
    int result;

    TEST_PRINT("\n--- Test: Region Contains Basic ---\n");
    test_region_reinit();

    /* SRAM buffer in SRAM region */
    result = tiku_region_contains(test_sram_pool, 64, TIKU_MEM_REGION_SRAM);
    TEST_ASSERT(result == 1, "SRAM buffer recognized as SRAM");

    /* NVM buffer in NVM region */
    result = tiku_region_contains(test_nvm_pool, 64, TIKU_MEM_REGION_NVM);
    TEST_ASSERT(result == 1, "NVM buffer recognized as NVM");

    /* NULL pointer rejected */
    result = tiku_region_contains(NULL, 64, TIKU_MEM_REGION_SRAM);
    TEST_ASSERT(result == 0, "NULL pointer rejected");

    /* Zero size rejected */
    result = tiku_region_contains(test_sram_pool, 0, TIKU_MEM_REGION_SRAM);
    TEST_ASSERT(result == 0, "zero size rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION CONTAINS WRONG TYPE                                          */
/*---------------------------------------------------------------------------*/

void test_region_contains_wrong_type(void)
{
    int result;

    TEST_PRINT("\n--- Test: Region Contains Wrong Type ---\n");
    test_region_reinit();

    /* SRAM buffer should not match NVM type */
    result = tiku_region_contains(test_sram_pool, 64, TIKU_MEM_REGION_NVM);
    TEST_ASSERT(result == 0, "SRAM buffer not NVM");

    /* NVM buffer should not match SRAM type */
    result = tiku_region_contains(test_nvm_pool, 64, TIKU_MEM_REGION_SRAM);
    TEST_ASSERT(result == 0, "NVM buffer not SRAM");

    /* SRAM buffer should not match PERIPHERAL type */
    result = tiku_region_contains(test_sram_pool, 64,
                                   TIKU_MEM_REGION_PERIPHERAL);
    TEST_ASSERT(result == 0, "SRAM buffer not PERIPHERAL");
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION CONTAINS BOUNDARY CONDITIONS                                 */
/*---------------------------------------------------------------------------*/

void test_region_contains_boundary(void)
{
    int result;

    /*
     * Install a custom region table whose boundaries exactly match the
     * test pools. On real hardware the platform SRAM region is much
     * larger than the 256-byte test pool, so boundary checks against
     * test_sram_pool edges would pass spuriously with the platform table.
     */
    const tiku_mem_region_t boundary_table[] = {
        { test_sram_pool, TEST_SRAM_POOL_SIZE, TIKU_MEM_REGION_SRAM },
        { test_nvm_pool,  TEST_NVM_POOL_SIZE,  TIKU_MEM_REGION_NVM  },
    };

    TEST_PRINT("\n--- Test: Region Contains Boundary ---\n");
    tiku_region_init(boundary_table, 2);

    /* Exact full region — should pass */
    result = tiku_region_contains(test_sram_pool, TEST_SRAM_POOL_SIZE,
                                   TIKU_MEM_REGION_SRAM);
    TEST_ASSERT(result == 1, "exact region boundary passes");

    /* One byte past the region end — should fail */
    result = tiku_region_contains(test_sram_pool, TEST_SRAM_POOL_SIZE + 1,
                                   TIKU_MEM_REGION_SRAM);
    TEST_ASSERT(result == 0, "one byte past region end fails");

    /* Range starting inside but extending past the end */
    result = tiku_region_contains(
        test_sram_pool + TEST_SRAM_POOL_SIZE - 4, 8,
        TIKU_MEM_REGION_SRAM);
    TEST_ASSERT(result == 0, "partial overflow past region end fails");

    /* Single byte at start of region */
    result = tiku_region_contains(test_sram_pool, 1,
                                   TIKU_MEM_REGION_SRAM);
    TEST_ASSERT(result == 1, "single byte at region start passes");

    /* Single byte at last valid position */
    result = tiku_region_contains(
        test_sram_pool + TEST_SRAM_POOL_SIZE - 1, 1,
        TIKU_MEM_REGION_SRAM);
    TEST_ASSERT(result == 1, "single byte at region end passes");

    /* Restore platform table for subsequent tests */
    test_region_reinit();
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION CONTAINS POINTER OVERFLOW PROTECTION                         */
/*---------------------------------------------------------------------------*/

void test_region_contains_overflow(void)
{
    uintptr_t near_max;
    const uint8_t *wrap_ptr;
    int result;

    TEST_PRINT("\n--- Test: Region Contains Overflow ---\n");
    test_region_reinit();

    /*
     * Craft a pointer near the top of the address space so that
     * ptr + size wraps around to zero. The overflow check in
     * tiku_region_contains must catch this.
     */
    near_max = ~(uintptr_t)0 - 5U;
    wrap_ptr = (const uint8_t *)near_max;

    result = tiku_region_contains(wrap_ptr, 20, TIKU_MEM_REGION_SRAM);
    TEST_ASSERT(result == 0, "wrapping pointer range rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION CLAIM AND UNCLAIM                                            */
/*---------------------------------------------------------------------------*/

void test_region_claim_unclaim(void)
{
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Region Claim and Unclaim ---\n");
    test_region_reinit();

    /* Claim a range in the SRAM pool */
    err = tiku_region_claim(test_sram_pool, 64, 1);
    TEST_ASSERT(err == TIKU_MEM_OK, "claim SRAM range succeeds");

    /* Unclaim it */
    err = tiku_region_unclaim(test_sram_pool);
    TEST_ASSERT(err == TIKU_MEM_OK, "unclaim succeeds");

    /* Unclaim same pointer again — should not be found */
    err = tiku_region_unclaim(test_sram_pool);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "unclaim already-freed returns NOT_FOUND");

    /* Unclaim NULL */
    err = tiku_region_unclaim(NULL);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "unclaim NULL returns NOT_FOUND");

    /* Can re-claim after unclaim */
    err = tiku_region_claim(test_sram_pool, 64, 2);
    TEST_ASSERT(err == TIKU_MEM_OK, "re-claim after unclaim succeeds");
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION CLAIM OVERLAP DETECTION                                      */
/*---------------------------------------------------------------------------*/

void test_region_claim_overlap(void)
{
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Region Claim Overlap ---\n");
    test_region_reinit();

    /* Claim [0, 64) */
    err = tiku_region_claim(test_sram_pool, 64, 1);
    TEST_ASSERT(err == TIKU_MEM_OK, "first claim succeeds");

    /* Overlapping claim [32, 96) — should fail */
    err = tiku_region_claim(test_sram_pool + 32, 64, 2);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID,
                "overlapping claim rejected");

    /* Exact same range — should also fail */
    err = tiku_region_claim(test_sram_pool, 64, 3);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID,
                "duplicate claim rejected");

    /* Non-overlapping claim [128, 192) — should succeed */
    err = tiku_region_claim(test_sram_pool + 128, 64, 4);
    TEST_ASSERT(err == TIKU_MEM_OK,
                "non-overlapping claim succeeds");

    /* Adjacent claim [64, 128) — no overlap, should succeed */
    err = tiku_region_claim(test_sram_pool + 64, 64, 5);
    TEST_ASSERT(err == TIKU_MEM_OK,
                "adjacent claim succeeds (no overlap)");
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION CLAIM OUTSIDE KNOWN MEMORY                                   */
/*---------------------------------------------------------------------------*/

void test_region_claim_unknown(void)
{
    tiku_mem_err_t err;
    const uint8_t *outside;

    /*
     * Use a custom region table matching the test pools so that
     * addresses past the pool end are truly outside all regions.
     * With the platform table, the SRAM region covers the full chip
     * SRAM and pool + POOL_SIZE would still be inside it.
     */
    const tiku_mem_region_t pool_table[] = {
        { test_sram_pool, TEST_SRAM_POOL_SIZE, TIKU_MEM_REGION_SRAM },
        { test_nvm_pool,  TEST_NVM_POOL_SIZE,  TIKU_MEM_REGION_NVM  },
    };

    TEST_PRINT("\n--- Test: Region Claim Unknown Memory ---\n");
    tiku_region_init(pool_table, 2);

    /* Address just past the end of the SRAM pool — not in any region */
    outside = test_sram_pool + TEST_SRAM_POOL_SIZE;
    err = tiku_region_claim(outside, 4, 1);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID,
                "claim outside all regions rejected");

    /* NULL pointer */
    err = tiku_region_claim(NULL, 4, 1);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID,
                "claim NULL pointer rejected");

    /* Zero size */
    err = tiku_region_claim(test_sram_pool, 0, 1);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID,
                "claim zero size rejected");

    /* Restore platform table */
    test_region_reinit();
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION CLAIM TABLE FULL                                             */
/*---------------------------------------------------------------------------*/

void test_region_claim_full(void)
{
    tiku_mem_err_t err;
    tiku_mem_arch_size_t i;

    TEST_PRINT("\n--- Test: Region Claim Table Full ---\n");
    test_region_reinit();

    /* Fill all claim slots with non-overlapping 8-byte ranges */
    for (i = 0; i < TIKU_REGION_MAX_CLAIMS; i++) {
        err = tiku_region_claim(test_sram_pool + i * 8, 8, (uint8_t)i);
        TEST_ASSERT(err == TIKU_MEM_OK, "claim slot succeeds");
    }

    /* One more should fail with FULL */
    err = tiku_region_claim(
        test_sram_pool + TIKU_REGION_MAX_CLAIMS * 8, 8, 99);
    TEST_ASSERT(err == TIKU_MEM_ERR_FULL,
                "claim beyond max returns FULL");
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION GET TYPE LOOKUP                                              */
/*---------------------------------------------------------------------------*/

void test_region_get_type_found(void)
{
    tiku_mem_region_type_t out_type;
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Region Get Type ---\n");
    test_region_reinit();

    /* Address in SRAM pool */
    err = tiku_region_get_type(test_sram_pool + 10, &out_type);
    TEST_ASSERT(err == TIKU_MEM_OK, "get_type for SRAM address succeeds");
    TEST_ASSERT(out_type == TIKU_MEM_REGION_SRAM, "type is SRAM");

    /* Address in NVM pool */
    err = tiku_region_get_type(test_nvm_pool + 10, &out_type);
    TEST_ASSERT(err == TIKU_MEM_OK, "get_type for NVM address succeeds");
    TEST_ASSERT(out_type == TIKU_MEM_REGION_NVM, "type is NVM");

    /* First byte of SRAM region */
    err = tiku_region_get_type(test_sram_pool, &out_type);
    TEST_ASSERT(err == TIKU_MEM_OK, "get_type at region base succeeds");
    TEST_ASSERT(out_type == TIKU_MEM_REGION_SRAM, "type at base is SRAM");

    /* Last byte of NVM region */
    err = tiku_region_get_type(
        test_nvm_pool + TEST_NVM_POOL_SIZE - 1, &out_type);
    TEST_ASSERT(err == TIKU_MEM_OK, "get_type at region end succeeds");
    TEST_ASSERT(out_type == TIKU_MEM_REGION_NVM, "type at end is NVM");
}

/*---------------------------------------------------------------------------*/
/* TEST: REGION GET TYPE NOT FOUND                                           */
/*---------------------------------------------------------------------------*/

void test_region_get_type_not_found(void)
{
    tiku_mem_region_type_t out_type;
    tiku_mem_err_t err;
    const uint8_t *outside;

    /*
     * Use a custom region table matching the test pools so that
     * pool + POOL_SIZE is truly outside all declared regions.
     */
    const tiku_mem_region_t pool_table[] = {
        { test_sram_pool, TEST_SRAM_POOL_SIZE, TIKU_MEM_REGION_SRAM },
        { test_nvm_pool,  TEST_NVM_POOL_SIZE,  TIKU_MEM_REGION_NVM  },
    };

    TEST_PRINT("\n--- Test: Region Get Type Not Found ---\n");
    tiku_region_init(pool_table, 2);

    /* Address past the end of the NVM pool */
    outside = test_nvm_pool + TEST_NVM_POOL_SIZE;
    err = tiku_region_get_type(outside, &out_type);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "address outside regions returns NOT_FOUND");

    /* NULL pointer */
    err = tiku_region_get_type(NULL, &out_type);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "NULL pointer returns NOT_FOUND");

    /* NULL output */
    err = tiku_region_get_type(test_sram_pool, NULL);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "NULL output returns NOT_FOUND");

    /* Restore platform table */
    test_region_reinit();
}
