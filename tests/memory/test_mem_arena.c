/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_mem_arena.c - Arena allocator tests
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
/* TEST 1: CREATION AND INITIAL STATS                                        */
/*---------------------------------------------------------------------------*/

void test_mem_create_and_stats(void)
{
    uint8_t *buf = test_sram_pool;
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Creation and Initial Stats ---\n");
    test_region_reinit();

    err = tiku_arena_create(&arena, buf, 64, 1);
    TEST_ASSERT(err == TIKU_MEM_OK, "arena_create returns OK");
    TEST_ASSERT(arena.active == 1, "arena is active after create");
    TEST_ASSERT(arena.id == 1, "arena ID is set correctly");

    err = tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(err == TIKU_MEM_OK, "arena_stats returns OK");
    TEST_ASSERT(stats.total_bytes == 64, "total_bytes matches buffer size");
    TEST_ASSERT(stats.used_bytes == 0, "used_bytes is 0 after create");
    TEST_ASSERT(stats.peak_bytes == 0, "peak_bytes is 0 after create");
    TEST_ASSERT(stats.alloc_count == 0, "alloc_count is 0 after create");
}

/*---------------------------------------------------------------------------*/
/* TEST 2: BASIC ALLOCATION AND POINTER CORRECTNESS                          */
/*---------------------------------------------------------------------------*/

void test_mem_basic_alloc(void)
{
    uint8_t *buf = test_sram_pool;
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    void *p1, *p2;

    TEST_PRINT("\n--- Test: Basic Allocation ---\n");
    test_region_reinit();

    tiku_arena_create(&arena, buf, 64, 2);

    p1 = tiku_arena_alloc(&arena, 8);
    TEST_ASSERT(p1 != NULL, "first alloc returns non-NULL");
    TEST_ASSERT(p1 == &buf[0], "first alloc starts at buffer base");

    p2 = tiku_arena_alloc(&arena, 4);
    TEST_ASSERT(p2 != NULL, "second alloc returns non-NULL");
    TEST_ASSERT(p2 == &buf[8], "second alloc follows first (8 bytes later)");

    /* Verify they don't overlap by writing to both */
    memset(p1, 0xAA, 8);
    memset(p2, 0xBB, 4);
    TEST_ASSERT(((uint8_t *)p1)[7] == 0xAA, "first alloc memory intact");
    TEST_ASSERT(((uint8_t *)p2)[0] == 0xBB, "second alloc memory intact");

    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.used_bytes == 12, "used_bytes tracks total allocated");
    TEST_ASSERT(stats.alloc_count == 2, "alloc_count tracks allocations");
}

/*---------------------------------------------------------------------------*/
/* TEST 3: ALIGNMENT OF ODD-SIZED REQUESTS                                   */
/*---------------------------------------------------------------------------*/

void test_mem_alignment(void)
{
    uint8_t *buf = test_sram_pool;
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    void *p1, *p2, *p3;
    const unsigned int A = TIKU_MEM_ARCH_ALIGNMENT;

    /* Compute expected aligned sizes for the three requests */
    const tiku_mem_arch_size_t a1 = TEST_ALIGN_UP(3);  /* 3 -> 4 */
    const tiku_mem_arch_size_t a2 = TEST_ALIGN_UP(1);  /* 1 -> A */
    const tiku_mem_arch_size_t a3 = TEST_ALIGN_UP(5);  /* 5 -> next multiple of A */

    TEST_PRINT("\n--- Test: %u-Byte Alignment ---\n", A);
    test_region_reinit();

    tiku_arena_create(&arena, buf, 64, 3);

    /* Request 3 bytes — rounded up to a1 */
    p1 = tiku_arena_alloc(&arena, 3);
    TEST_ASSERT(p1 == &buf[0], "odd alloc (3) starts at base");

    /* Next alloc should start at offset a1, not 3 */
    p2 = tiku_arena_alloc(&arena, 1);
    TEST_ASSERT(p2 == &buf[a1], "next alloc after 3-byte request starts at aligned offset");

    /* Request 5 bytes — rounded to a3 */
    p3 = tiku_arena_alloc(&arena, 5);
    TEST_ASSERT(p3 == &buf[a1 + a2], "5-byte request starts at correct aligned offset");

    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.used_bytes == a1 + a2 + a3,
                "used_bytes reflects aligned sizes");

    /* Verify all returned pointers meet platform alignment */
    TEST_ASSERT(((uintptr_t)p1 % A) == 0, "pointer 1 is aligned");
    TEST_ASSERT(((uintptr_t)p2 % A) == 0, "pointer 2 is aligned");
    TEST_ASSERT(((uintptr_t)p3 % A) == 0, "pointer 3 is aligned");
}

/*---------------------------------------------------------------------------*/
/* TEST 4: ARENA FULL RETURNS NULL                                           */
/*---------------------------------------------------------------------------*/

void test_mem_arena_full(void)
{
    /*
     * Use a buffer whose size is 4 * alignment so the test works
     * identically on 2-byte (MSP430) and 4-byte (host/ARM) targets.
     */
    const unsigned int A = TIKU_MEM_ARCH_ALIGNMENT;
    uint8_t *buf = test_sram_pool;
    tiku_mem_arch_size_t buf_size = 4 * A;
    tiku_arena_t arena;
    void *p1, *p2, *p3;

    TEST_PRINT("\n--- Test: Arena Full ---\n");
    test_region_reinit();

    tiku_arena_create(&arena, buf, buf_size, 4);

    /* Allocate 3*A bytes — fits, leaves exactly A bytes */
    p1 = tiku_arena_alloc(&arena, 3 * A);
    TEST_ASSERT(p1 != NULL, "3*A alloc succeeds in 4*A arena");

    /* Only A bytes remain — request 2*A should fail */
    p2 = tiku_arena_alloc(&arena, 2 * A);
    TEST_ASSERT(p2 == NULL, "2*A alloc fails when only A bytes remain");

    /* Request exactly A bytes — should succeed */
    p3 = tiku_arena_alloc(&arena, A);
    TEST_ASSERT(p3 != NULL, "A-byte alloc succeeds with exactly A bytes left");

    /* Arena is now completely full */
    p2 = tiku_arena_alloc(&arena, 1);
    TEST_ASSERT(p2 == NULL, "1-byte alloc fails when arena is full");
}

/*---------------------------------------------------------------------------*/
/* TEST 5: RESET RESTORES OFFSET BUT PRESERVES PEAK                         */
/*---------------------------------------------------------------------------*/

void test_mem_reset(void)
{
    uint8_t *buf = test_sram_pool;
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Reset ---\n");
    test_region_reinit();

    tiku_arena_create(&arena, buf, 64, 5);

    /* Allocate 20 bytes (sizes already aligned to any power-of-2) */
    tiku_arena_alloc(&arena, 12);
    tiku_arena_alloc(&arena, 8);

    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.used_bytes == 20, "20 bytes used before reset");
    TEST_ASSERT(stats.alloc_count == 2, "2 allocations before reset");

    /* Reset */
    err = tiku_arena_reset(&arena);
    TEST_ASSERT(err == TIKU_MEM_OK, "arena_reset returns OK");

    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.used_bytes == 0, "used_bytes is 0 after reset");
    TEST_ASSERT(stats.alloc_count == 0, "alloc_count is 0 after reset");
    TEST_ASSERT(stats.peak_bytes == 20, "peak_bytes preserved after reset");

    /* Can allocate again from the beginning */
    void *p = tiku_arena_alloc(&arena, 4);
    TEST_ASSERT(p == &buf[0], "allocation after reset starts at base");
}

/*---------------------------------------------------------------------------*/
/* TEST 6: PEAK TRACKS LIFETIME MAXIMUM ACROSS RESETS                        */
/*---------------------------------------------------------------------------*/

void test_mem_peak_tracking(void)
{
    uint8_t *buf = test_sram_pool;
    tiku_arena_t arena;
    tiku_mem_stats_t stats;

    TEST_PRINT("\n--- Test: Peak Tracking Across Resets ---\n");
    test_region_reinit();

    tiku_arena_create(&arena, buf, 64, 6);

    /* Cycle 1: allocate 12 bytes */
    tiku_arena_alloc(&arena, 12);
    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.peak_bytes == 12, "peak is 12 after first cycle");

    tiku_arena_reset(&arena);

    /* Cycle 2: allocate 32 bytes — new peak */
    tiku_arena_alloc(&arena, 20);
    tiku_arena_alloc(&arena, 12);
    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.peak_bytes == 32, "peak is 32 after second cycle");

    tiku_arena_reset(&arena);

    /* Cycle 3: allocate only 8 bytes — peak should remain 32 */
    tiku_arena_alloc(&arena, 8);
    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.peak_bytes == 32, "peak remains 32 after smaller third cycle");
    TEST_ASSERT(stats.used_bytes == 8, "used_bytes is 8 in third cycle");
}

/*---------------------------------------------------------------------------*/
/* TEST 7: NULL AND ZERO-SIZE INPUTS REJECTED                                */
/*---------------------------------------------------------------------------*/

void test_mem_invalid_inputs(void)
{
    uint8_t *buf = test_sram_pool;
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;
    void *p;

    TEST_PRINT("\n--- Test: Invalid Inputs ---\n");
    test_region_reinit();

    /* NULL arena pointer */
    err = tiku_arena_create(NULL, buf, 32, 0);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "create with NULL arena rejected");

    /* NULL buffer pointer */
    err = tiku_arena_create(&arena, NULL, 32, 0);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "create with NULL buffer rejected");

    /* Alloc from NULL arena */
    p = tiku_arena_alloc(NULL, 4);
    TEST_ASSERT(p == NULL, "alloc from NULL arena returns NULL");

    /* Alloc zero bytes */
    tiku_arena_create(&arena, buf, 32, 7);
    p = tiku_arena_alloc(&arena, 0);
    TEST_ASSERT(p == NULL, "alloc of 0 bytes returns NULL");

    /* Reset NULL arena */
    err = tiku_arena_reset(NULL);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "reset NULL arena rejected");

    /* Stats with NULL arena */
    err = tiku_arena_stats(NULL, &stats);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "stats with NULL arena rejected");

    /* Stats with NULL output */
    err = tiku_arena_stats(&arena, NULL);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "stats with NULL output rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST 8: SECURE RESET ZEROS MEMORY AND RESETS STATE                         */
/*---------------------------------------------------------------------------*/

void test_mem_secure_reset(void)
{
    uint8_t *buf = test_sram_pool;
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;
    int all_zero;
    unsigned int i;
    void *p;

    TEST_PRINT("\n--- Test: Secure Reset ---\n");
    test_region_reinit();

    tiku_arena_create(&arena, buf, 32, 8);

    /* Fill the arena with a known pattern */
    p = tiku_arena_alloc(&arena, 32);
    TEST_ASSERT(p != NULL, "allocate full buffer");
    memset(p, 0xAA, 32);

    /* Verify non-zero before secure reset */
    TEST_ASSERT(buf[0] == 0xAA, "buffer has data before secure reset");
    TEST_ASSERT(buf[31] == 0xAA, "buffer end has data before secure reset");

    /* Secure reset */
    err = tiku_arena_secure_reset(&arena);
    TEST_ASSERT(err == TIKU_MEM_OK, "secure_reset returns OK");

    /* Verify every byte is zero */
    all_zero = 1;
    for (i = 0; i < 32; i++) {
        if (buf[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    TEST_ASSERT(all_zero, "all bytes zeroed after secure reset");

    /* Verify arena state is reset */
    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.used_bytes == 0, "used_bytes is 0 after secure reset");
    TEST_ASSERT(stats.alloc_count == 0, "alloc_count is 0 after secure reset");
    TEST_ASSERT(stats.peak_bytes == 32, "peak_bytes preserved after secure reset");

    /* Can allocate again from the beginning */
    p = tiku_arena_alloc(&arena, 4);
    TEST_ASSERT(p == &buf[0], "allocation after secure reset starts at base");

    /* NULL arena rejected */
    err = tiku_arena_secure_reset(NULL);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "secure_reset NULL arena rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST 9: TWO INDEPENDENT ARENAS                                            */
/*---------------------------------------------------------------------------*/

void test_mem_two_arenas(void)
{
    uint8_t *buf_a = test_sram_pool;
    uint8_t *buf_b = test_sram_pool + 128;
    tiku_arena_t arena_a, arena_b;
    tiku_mem_stats_t stats_a, stats_b;
    void *pa, *pb;

    TEST_PRINT("\n--- Test: Two Independent Arenas ---\n");
    test_region_reinit();

    tiku_arena_create(&arena_a, buf_a, 32, 10);
    tiku_arena_create(&arena_b, buf_b, 64, 20);

    /* Allocate different amounts from each */
    pa = tiku_arena_alloc(&arena_a, 8);
    pb = tiku_arena_alloc(&arena_b, 16);

    TEST_ASSERT(pa == &buf_a[0], "arena A alloc from buf_a");
    TEST_ASSERT(pb == &buf_b[0], "arena B alloc from buf_b");

    tiku_arena_stats(&arena_a, &stats_a);
    tiku_arena_stats(&arena_b, &stats_b);

    TEST_ASSERT(stats_a.used_bytes == 8, "arena A used 8 bytes");
    TEST_ASSERT(stats_b.used_bytes == 16, "arena B used 16 bytes");
    TEST_ASSERT(stats_a.total_bytes == 32, "arena A total is 32");
    TEST_ASSERT(stats_b.total_bytes == 64, "arena B total is 64");
    TEST_ASSERT(stats_a.alloc_count == 1, "arena A has 1 allocation");
    TEST_ASSERT(stats_b.alloc_count == 1, "arena B has 1 allocation");

    /* Reset A — B should be unaffected */
    tiku_arena_reset(&arena_a);
    tiku_arena_stats(&arena_a, &stats_a);
    tiku_arena_stats(&arena_b, &stats_b);

    TEST_ASSERT(stats_a.used_bytes == 0, "arena A reset to 0");
    TEST_ASSERT(stats_b.used_bytes == 16, "arena B unaffected by A's reset");

    /* Allocate from A again — should not affect B */
    pa = tiku_arena_alloc(&arena_a, 4);
    TEST_ASSERT(pa == &buf_a[0], "arena A reallocates from base after reset");

    tiku_arena_stats(&arena_b, &stats_b);
    TEST_ASSERT(stats_b.used_bytes == 16, "arena B still unaffected");
}
