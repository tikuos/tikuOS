/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_mem_pool.c - Pool allocator tests
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
/* TEST 32: POOL CREATION AND INITIAL STATS                                  */
/*---------------------------------------------------------------------------*/

void test_pool_create_and_stats(void)
{
    uint8_t buf[64];
    tiku_pool_t pool;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Pool Creation and Initial Stats ---\n");

    err = tiku_pool_create(&pool, buf, 8, 4, 1);
    TEST_ASSERT(err == TIKU_MEM_OK, "pool_create returns OK");
    TEST_ASSERT(pool.active == 1, "pool is active after create");
    TEST_ASSERT(pool.id == 1, "pool ID is set correctly");
    TEST_ASSERT(pool.block_count == 4, "block_count is 4");
    TEST_ASSERT(pool.used_count == 0, "used_count is 0 after create");
    TEST_ASSERT(pool.free_head != NULL, "free_head is non-NULL after create");

    err = tiku_pool_stats(&pool, &stats);
    TEST_ASSERT(err == TIKU_MEM_OK, "pool_stats returns OK");
    TEST_ASSERT(stats.total_bytes == pool.block_size * 4,
                "total_bytes matches block_size * count");
    TEST_ASSERT(stats.used_bytes == 0, "used_bytes is 0 after create");
    TEST_ASSERT(stats.peak_bytes == 0, "peak_bytes is 0 after create");
    TEST_ASSERT(stats.alloc_count == 0, "alloc_count is 0 after create");
}

/*---------------------------------------------------------------------------*/
/* TEST 33: BASIC ALLOC AND FREE                                             */
/*---------------------------------------------------------------------------*/

void test_pool_basic_alloc_free(void)
{
    uint8_t buf[64];
    tiku_pool_t pool;
    tiku_mem_stats_t stats;
    void *p1, *p2;
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Pool Basic Alloc and Free ---\n");

    tiku_pool_create(&pool, buf, 8, 4, 2);

    p1 = tiku_pool_alloc(&pool);
    TEST_ASSERT(p1 != NULL, "first alloc returns non-NULL");

    p2 = tiku_pool_alloc(&pool);
    TEST_ASSERT(p2 != NULL, "second alloc returns non-NULL");
    TEST_ASSERT(p2 != p1, "second alloc returns different pointer");

    /* Verify they don't overlap by writing to both */
    memset(p1, 0xAA, 8);
    memset(p2, 0xBB, 8);
    TEST_ASSERT(((uint8_t *)p1)[7] == 0xAA, "first alloc memory intact");
    TEST_ASSERT(((uint8_t *)p2)[0] == 0xBB, "second alloc memory intact");

    tiku_pool_stats(&pool, &stats);
    TEST_ASSERT(stats.used_bytes == pool.block_size * 2,
                "used_bytes tracks 2 blocks");
    TEST_ASSERT(stats.alloc_count == 2, "alloc_count tracks allocations");

    /* Free the first block */
    err = tiku_pool_free(&pool, p1);
    TEST_ASSERT(err == TIKU_MEM_OK, "pool_free returns OK");

    tiku_pool_stats(&pool, &stats);
    TEST_ASSERT(stats.alloc_count == 1, "alloc_count is 1 after free");
}

/*---------------------------------------------------------------------------*/
/* TEST 34: POOL EXHAUSTION                                                  */
/*---------------------------------------------------------------------------*/

void test_pool_exhaustion(void)
{
    /*
     * Buffer must be large enough for 4 blocks at the effective block
     * size.  On a 64-bit host sizeof(void *) == 8 but
     * TIKU_MEM_ARCH_ALIGNMENT == 4, so the minimum block size is 8.
     * Use sizeof(void *) as the requested block size to avoid clamping
     * surprises, and size the buffer accordingly.
     */
    uint8_t buf[4 * sizeof(void *)];
    tiku_pool_t pool;
    void *blocks[4];
    void *p;
    int i;

    TEST_PRINT("\n--- Test: Pool Exhaustion ---\n");

    tiku_pool_create(&pool, buf, (tiku_mem_arch_size_t)sizeof(void *), 4, 3);

    /* Allocate all 4 blocks */
    for (i = 0; i < 4; i++) {
        blocks[i] = tiku_pool_alloc(&pool);
        TEST_ASSERT(blocks[i] != NULL, "block alloc succeeds");
    }

    /* Pool is now empty — next alloc should fail */
    p = tiku_pool_alloc(&pool);
    TEST_ASSERT(p == NULL, "alloc from exhausted pool returns NULL");

    /* Free one block — next alloc should succeed */
    tiku_pool_free(&pool, blocks[0]);
    p = tiku_pool_alloc(&pool);
    TEST_ASSERT(p != NULL, "alloc succeeds after free returns a block");
}

/*---------------------------------------------------------------------------*/
/* TEST 35: FREE VALIDATION — OUT OF RANGE                                   */
/*---------------------------------------------------------------------------*/

void test_pool_free_out_of_range(void)
{
    uint8_t buf[32];
    uint8_t other_buf[8];
    tiku_pool_t pool;
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Pool Free Out Of Range ---\n");

    tiku_pool_create(&pool, buf, 8, 4, 4);

    /* Free a pointer from a completely different buffer */
    err = tiku_pool_free(&pool, other_buf);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID,
                "free of out-of-range pointer rejected");

    /* Free a pointer just past the pool's buffer end */
    err = tiku_pool_free(&pool, buf + 32);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID,
                "free of pointer past buffer end rejected");

    /* Free a pointer before the pool's buffer start */
    err = tiku_pool_free(&pool, buf - 1);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID,
                "free of pointer before buffer start rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST 36: FREE VALIDATION — MISALIGNED POINTER                             */
/*---------------------------------------------------------------------------*/

void test_pool_free_misaligned(void)
{
    uint8_t buf[64];
    tiku_pool_t pool;
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Pool Free Misaligned ---\n");

    tiku_pool_create(&pool, buf, 8, 4, 5);

    /* Free a pointer within the buffer but not on a block boundary */
    err = tiku_pool_free(&pool, buf + 1);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID,
                "free of misaligned pointer (offset 1) rejected");

    err = tiku_pool_free(&pool, buf + pool.block_size + 1);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID,
                "free of misaligned pointer (block+1) rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST 37: ALLOC-FREE-REALLOC (LIFO FREELIST)                               */
/*---------------------------------------------------------------------------*/

void test_pool_alloc_free_realloc(void)
{
    uint8_t buf[64];
    tiku_pool_t pool;
    void *p1, *p2, *p3;

    TEST_PRINT("\n--- Test: Pool Alloc-Free-Realloc (LIFO) ---\n");

    tiku_pool_create(&pool, buf, 8, 4, 6);

    p1 = tiku_pool_alloc(&pool);
    p2 = tiku_pool_alloc(&pool);

    /* Free p2 (most recent), then p1 */
    tiku_pool_free(&pool, p2);
    tiku_pool_free(&pool, p1);

    /* LIFO: next alloc should return p1 (last freed = first out) */
    p3 = tiku_pool_alloc(&pool);
    TEST_ASSERT(p3 == p1, "LIFO: re-alloc returns last freed block");

    p3 = tiku_pool_alloc(&pool);
    TEST_ASSERT(p3 == p2, "LIFO: second re-alloc returns previously freed block");
}

/*---------------------------------------------------------------------------*/
/* TEST 38: PEAK TRACKING                                                    */
/*---------------------------------------------------------------------------*/

void test_pool_peak_tracking(void)
{
    uint8_t buf[64];
    tiku_pool_t pool;
    tiku_mem_stats_t stats;
    void *p1, *p2, *p3;

    TEST_PRINT("\n--- Test: Pool Peak Tracking ---\n");

    tiku_pool_create(&pool, buf, 8, 4, 7);

    /* Allocate 3 blocks — peak should be 3 */
    p1 = tiku_pool_alloc(&pool);
    p2 = tiku_pool_alloc(&pool);
    p3 = tiku_pool_alloc(&pool);

    tiku_pool_stats(&pool, &stats);
    TEST_ASSERT(stats.peak_bytes == pool.block_size * 3,
                "peak is 3 blocks after 3 allocs");

    /* Free all 3 */
    tiku_pool_free(&pool, p1);
    tiku_pool_free(&pool, p2);
    tiku_pool_free(&pool, p3);

    tiku_pool_stats(&pool, &stats);
    TEST_ASSERT(stats.used_bytes == 0, "used is 0 after freeing all");
    TEST_ASSERT(stats.peak_bytes == pool.block_size * 3,
                "peak preserved after freeing all blocks");

    /* Allocate only 1 — peak should still be 3 */
    tiku_pool_alloc(&pool);

    tiku_pool_stats(&pool, &stats);
    TEST_ASSERT(stats.peak_bytes == pool.block_size * 3,
                "peak remains 3 after smaller reuse");
}

/*---------------------------------------------------------------------------*/
/* TEST 39: RESET RESTORES FREELIST AND PRESERVES PEAK                       */
/*---------------------------------------------------------------------------*/

void test_pool_reset(void)
{
    uint8_t buf[64];
    tiku_pool_t pool;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;
    void *p;

    TEST_PRINT("\n--- Test: Pool Reset ---\n");

    tiku_pool_create(&pool, buf, 8, 4, 8);

    /* Allocate all 4 blocks */
    tiku_pool_alloc(&pool);
    tiku_pool_alloc(&pool);
    tiku_pool_alloc(&pool);
    tiku_pool_alloc(&pool);

    tiku_pool_stats(&pool, &stats);
    TEST_ASSERT(stats.alloc_count == 4, "4 blocks used before reset");

    /* Reset — all blocks return to freelist */
    err = tiku_pool_reset(&pool);
    TEST_ASSERT(err == TIKU_MEM_OK, "pool_reset returns OK");

    tiku_pool_stats(&pool, &stats);
    TEST_ASSERT(stats.used_bytes == 0, "used_bytes is 0 after reset");
    TEST_ASSERT(stats.alloc_count == 0, "alloc_count is 0 after reset");
    TEST_ASSERT(stats.peak_bytes == pool.block_size * 4,
                "peak preserved after reset");

    /* Can allocate all 4 blocks again */
    p = tiku_pool_alloc(&pool);
    TEST_ASSERT(p != NULL, "allocation succeeds after reset");
    TEST_ASSERT(p == &buf[0], "first alloc after reset starts at base");
}

/*---------------------------------------------------------------------------*/
/* TEST 40: INVALID INPUTS                                                   */
/*---------------------------------------------------------------------------*/

void test_pool_invalid_inputs(void)
{
    uint8_t buf[32];
    tiku_pool_t pool;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;
    void *p;

    TEST_PRINT("\n--- Test: Pool Invalid Inputs ---\n");

    /* NULL pool */
    err = tiku_pool_create(NULL, buf, 8, 4, 0);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "create with NULL pool rejected");

    /* NULL buffer */
    err = tiku_pool_create(&pool, NULL, 8, 4, 0);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "create with NULL buffer rejected");

    /* Zero block count */
    err = tiku_pool_create(&pool, buf, 8, 0, 0);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "create with zero count rejected");

    /* Alloc from NULL pool */
    p = tiku_pool_alloc(NULL);
    TEST_ASSERT(p == NULL, "alloc from NULL pool returns NULL");

    /* Free to NULL pool */
    err = tiku_pool_free(NULL, buf);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "free to NULL pool rejected");

    /* Free NULL pointer */
    tiku_pool_create(&pool, buf, 8, 4, 0);
    err = tiku_pool_free(&pool, NULL);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "free NULL pointer rejected");

    /* Reset NULL pool */
    err = tiku_pool_reset(NULL);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "reset NULL pool rejected");

    /* Stats with NULL pool */
    err = tiku_pool_stats(NULL, &stats);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "stats with NULL pool rejected");

    /* Stats with NULL output */
    err = tiku_pool_stats(&pool, NULL);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "stats with NULL output rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST 41: TWO INDEPENDENT POOLS                                            */
/*---------------------------------------------------------------------------*/

void test_pool_two_pools(void)
{
    uint8_t buf_a[32];
    uint8_t buf_b[64];
    tiku_pool_t pool_a, pool_b;
    tiku_mem_stats_t stats_a, stats_b;
    void *pa, *pb;

    TEST_PRINT("\n--- Test: Two Independent Pools ---\n");

    tiku_pool_create(&pool_a, buf_a, 8, 4, 10);
    tiku_pool_create(&pool_b, buf_b, 16, 4, 20);

    /* Allocate from both */
    pa = tiku_pool_alloc(&pool_a);
    pb = tiku_pool_alloc(&pool_b);

    TEST_ASSERT(pa != NULL, "pool A alloc succeeds");
    TEST_ASSERT(pb != NULL, "pool B alloc succeeds");

    tiku_pool_stats(&pool_a, &stats_a);
    tiku_pool_stats(&pool_b, &stats_b);

    TEST_ASSERT(stats_a.alloc_count == 1, "pool A has 1 allocation");
    TEST_ASSERT(stats_b.alloc_count == 1, "pool B has 1 allocation");

    /* Reset A — B should be unaffected */
    tiku_pool_reset(&pool_a);
    tiku_pool_stats(&pool_a, &stats_a);
    tiku_pool_stats(&pool_b, &stats_b);

    TEST_ASSERT(stats_a.alloc_count == 0, "pool A reset to 0");
    TEST_ASSERT(stats_b.alloc_count == 1, "pool B unaffected by A's reset");

    /* Free from B — A should be unaffected */
    tiku_pool_free(&pool_b, pb);
    tiku_pool_stats(&pool_b, &stats_b);
    TEST_ASSERT(stats_b.alloc_count == 0, "pool B free works independently");
}

/*---------------------------------------------------------------------------*/
/* TEST 42: BLOCK SIZE ALIGNMENT AND MINIMUM ENFORCEMENT                     */
/*---------------------------------------------------------------------------*/

void test_pool_block_size_alignment(void)
{
    uint8_t buf[64];
    tiku_pool_t pool;
    const tiku_mem_arch_size_t A = TIKU_MEM_ARCH_ALIGNMENT;
    tiku_mem_arch_size_t min_size;

    TEST_PRINT("\n--- Test: Pool Block Size Alignment ---\n");

    /* Request block_size = 1 — should be clamped to minimum */
    tiku_pool_create(&pool, buf, 1, 4, 9);

    min_size = TEST_ALIGN_UP((tiku_mem_arch_size_t)sizeof(void *));
    if (A > min_size) {
        min_size = A;
    }
    min_size = TEST_ALIGN_UP(min_size);

    TEST_ASSERT(pool.block_size >= min_size,
                "block_size clamped to minimum (freelist pointer)");
    TEST_ASSERT(pool.block_size % A == 0,
                "block_size is aligned to platform alignment");

    /* Request odd block_size — should be rounded up */
    tiku_pool_create(&pool, buf, 7, 4, 9);
    TEST_ASSERT(pool.block_size == TEST_ALIGN_UP(7),
                "odd block_size rounded up to alignment");
    TEST_ASSERT(pool.block_size % A == 0,
                "rounded block_size is properly aligned");
}

/*---------------------------------------------------------------------------*/
/* TEST 43: STATS MAPPING                                                    */
/*---------------------------------------------------------------------------*/

void test_pool_stats_mapping(void)
{
    uint8_t buf[64];
    tiku_pool_t pool;
    tiku_mem_stats_t stats;

    TEST_PRINT("\n--- Test: Pool Stats Mapping ---\n");

    tiku_pool_create(&pool, buf, 8, 4, 11);

    /* Allocate 2 blocks */
    tiku_pool_alloc(&pool);
    tiku_pool_alloc(&pool);

    tiku_pool_stats(&pool, &stats);

    TEST_ASSERT(stats.total_bytes == pool.block_size * pool.block_count,
                "total_bytes = block_size * block_count");
    TEST_ASSERT(stats.used_bytes == pool.block_size * 2,
                "used_bytes = block_size * used_count");
    TEST_ASSERT(stats.alloc_count == 2,
                "alloc_count = used_count");
    TEST_ASSERT(stats.peak_bytes == pool.block_size * 2,
                "peak_bytes = block_size * peak_count");
}

/*---------------------------------------------------------------------------*/
/* TEST 44: DEBUG POISONING                                                  */
/*---------------------------------------------------------------------------*/

void test_pool_debug_poisoning(void)
{
    uint8_t buf[128];
    tiku_pool_t pool;
    void *p;
    uint8_t *block;
    tiku_mem_arch_size_t i;
    int all_poisoned;

    TEST_PRINT("\n--- Test: Pool Debug Poisoning ---\n");

#if TIKU_POOL_DEBUG
    /*
     * Use a block_size larger than sizeof(void *) so there are bytes
     * after the freelist pointer to check.  On 64-bit hosts
     * sizeof(void *) == 8, so use 16 to guarantee at least 8 poisoned
     * bytes regardless of platform.
     */
    tiku_pool_create(&pool, buf, 16, 4, 12);

    /* Allocate and fill with a known pattern */
    p = tiku_pool_alloc(&pool);
    memset(p, 0xAA, pool.block_size);

    /* Free the block — should poison with 0xDE after freelist ptr */
    tiku_pool_free(&pool, p);

    block = (uint8_t *)p;
    all_poisoned = 1;
    for (i = (tiku_mem_arch_size_t)sizeof(void *); i < pool.block_size; i++) {
        if (block[i] != 0xDE) {
            all_poisoned = 0;
            break;
        }
    }
    TEST_ASSERT(all_poisoned, "freed bytes after freelist ptr poisoned with 0xDE");
    TEST_ASSERT(pool.block_size > (tiku_mem_arch_size_t)sizeof(void *),
                "block has bytes beyond freelist pointer to poison");
#else
    (void)buf;
    (void)pool;
    (void)p;
    (void)block;
    (void)i;
    (void)all_poisoned;
    TEST_PRINT("  SKIP: TIKU_POOL_DEBUG is disabled\n");
    TEST_ASSERT(1, "debug poisoning test skipped (TIKU_POOL_DEBUG=0)");
#endif
}

/*---------------------------------------------------------------------------*/
/* TEST 45: ALLOC RETURNS BLOCKS WITHIN BUFFER                               */
/*---------------------------------------------------------------------------*/

void test_pool_alloc_within_buffer(void)
{
    uint8_t buf[64];
    tiku_pool_t pool;
    void *p;
    int i;

    TEST_PRINT("\n--- Test: Pool Alloc Within Buffer ---\n");

    tiku_pool_create(&pool, buf, 8, 4, 13);

    for (i = 0; i < 4; i++) {
        p = tiku_pool_alloc(&pool);
        TEST_ASSERT((uint8_t *)p >= buf, "block is at or after buffer start");
        TEST_ASSERT((uint8_t *)p + pool.block_size <=
                    buf + pool.block_size * pool.block_count,
                    "block end is within buffer");
    }
}
