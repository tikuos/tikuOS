/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_tiku_mem.c - Arena allocator tests (host-compilable)
 *
 * Standalone test program for the memory management module. Compiles
 * and runs on a host machine with GCC — no MSP430 toolchain needed.
 *
 *   gcc -std=c99 -Wall -Wextra -I. \
 *       tests/test_tiku_mem.c kernel/memory/tiku_mem.c \
 *       -o test_tiku_mem && ./test_tiku_mem
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kernel/memory/tiku_mem.h"

/*---------------------------------------------------------------------------*/
/* TEST HARNESS                                                              */
/*---------------------------------------------------------------------------*/

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg)                                              \
    do {                                                                    \
        tests_run++;                                                        \
        if (cond) {                                                         \
            tests_passed++;                                                 \
            printf("  PASS: %s\n", msg);                                    \
        } else {                                                            \
            tests_failed++;                                                 \
            printf("  FAIL: %s\n", msg);                                    \
        }                                                                   \
    } while (0)

/*---------------------------------------------------------------------------*/
/* TEST 1: CREATION AND INITIAL STATS                                        */
/*---------------------------------------------------------------------------*/

static void test_create_and_stats(void)
{
    uint8_t buf[64];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;

    printf("\n--- Test: Creation and Initial Stats ---\n");

    err = tiku_arena_create(&arena, buf, sizeof(buf), 1);
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

static void test_basic_alloc(void)
{
    uint8_t buf[64];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    void *p1, *p2;

    printf("\n--- Test: Basic Allocation ---\n");

    tiku_arena_create(&arena, buf, sizeof(buf), 2);

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
/* TEST 3: 2-BYTE ALIGNMENT OF ODD-SIZED REQUESTS                           */
/*---------------------------------------------------------------------------*/

static void test_alignment(void)
{
    uint8_t buf[64];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    void *p1, *p2, *p3;

    printf("\n--- Test: 2-Byte Alignment ---\n");

    tiku_arena_create(&arena, buf, sizeof(buf), 3);

    /* Request 3 bytes — should be rounded up to 4 */
    p1 = tiku_arena_alloc(&arena, 3);
    TEST_ASSERT(p1 == &buf[0], "odd alloc (3) starts at base");

    /* Next alloc should start at offset 4, not 3 */
    p2 = tiku_arena_alloc(&arena, 1);
    TEST_ASSERT(p2 == &buf[4], "next alloc after 3-byte request starts at offset 4");

    /* Request 5 bytes — rounded to 6, next starts at 4+2+6 = offset 12 */
    p3 = tiku_arena_alloc(&arena, 5);
    TEST_ASSERT(p3 == &buf[6], "5-byte request starts at offset 6 (after 4+2)");

    tiku_arena_stats(&arena, &stats);
    /* 4 + 2 + 6 = 12 bytes used (aligned sizes) */
    TEST_ASSERT(stats.used_bytes == 12, "used_bytes reflects aligned sizes (4+2+6=12)");

    /* Verify all returned pointers are 2-byte aligned */
    TEST_ASSERT(((uintptr_t)p1 % 2) == 0, "pointer 1 is 2-byte aligned");
    TEST_ASSERT(((uintptr_t)p2 % 2) == 0, "pointer 2 is 2-byte aligned");
    TEST_ASSERT(((uintptr_t)p3 % 2) == 0, "pointer 3 is 2-byte aligned");
}

/*---------------------------------------------------------------------------*/
/* TEST 4: ARENA FULL RETURNS NULL                                           */
/*---------------------------------------------------------------------------*/

static void test_arena_full(void)
{
    uint8_t buf[8];
    tiku_arena_t arena;
    void *p1, *p2, *p3;

    printf("\n--- Test: Arena Full ---\n");

    tiku_arena_create(&arena, buf, sizeof(buf), 4);

    /* Allocate 6 bytes (aligned to 6) — fits in 8-byte buffer */
    p1 = tiku_arena_alloc(&arena, 6);
    TEST_ASSERT(p1 != NULL, "6-byte alloc succeeds in 8-byte arena");

    /* Only 2 bytes remain — request 4 should fail */
    p2 = tiku_arena_alloc(&arena, 4);
    TEST_ASSERT(p2 == NULL, "4-byte alloc fails when only 2 bytes remain");

    /* Request exactly 2 bytes — should succeed */
    p3 = tiku_arena_alloc(&arena, 2);
    TEST_ASSERT(p3 != NULL, "2-byte alloc succeeds with exactly 2 bytes left");

    /* Arena is now completely full */
    p2 = tiku_arena_alloc(&arena, 1);
    TEST_ASSERT(p2 == NULL, "1-byte alloc fails when arena is full");
}

/*---------------------------------------------------------------------------*/
/* TEST 5: RESET RESTORES OFFSET BUT PRESERVES PEAK                         */
/*---------------------------------------------------------------------------*/

static void test_reset(void)
{
    uint8_t buf[64];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;

    printf("\n--- Test: Reset ---\n");

    tiku_arena_create(&arena, buf, sizeof(buf), 5);

    /* Allocate 20 bytes */
    tiku_arena_alloc(&arena, 10);
    tiku_arena_alloc(&arena, 10);

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

static void test_peak_tracking(void)
{
    uint8_t buf[64];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;

    printf("\n--- Test: Peak Tracking Across Resets ---\n");

    tiku_arena_create(&arena, buf, sizeof(buf), 6);

    /* Cycle 1: allocate 10 bytes */
    tiku_arena_alloc(&arena, 10);
    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.peak_bytes == 10, "peak is 10 after first cycle");

    tiku_arena_reset(&arena);

    /* Cycle 2: allocate 30 bytes — new peak */
    tiku_arena_alloc(&arena, 20);
    tiku_arena_alloc(&arena, 10);
    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.peak_bytes == 30, "peak is 30 after second cycle");

    tiku_arena_reset(&arena);

    /* Cycle 3: allocate only 8 bytes — peak should remain 30 */
    tiku_arena_alloc(&arena, 8);
    tiku_arena_stats(&arena, &stats);
    TEST_ASSERT(stats.peak_bytes == 30, "peak remains 30 after smaller third cycle");
    TEST_ASSERT(stats.used_bytes == 8, "used_bytes is 8 in third cycle");
}

/*---------------------------------------------------------------------------*/
/* TEST 7: NULL AND ZERO-SIZE INPUTS REJECTED                                */
/*---------------------------------------------------------------------------*/

static void test_invalid_inputs(void)
{
    uint8_t buf[32];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;
    void *p;

    printf("\n--- Test: Invalid Inputs ---\n");

    /* NULL arena pointer */
    err = tiku_arena_create(NULL, buf, sizeof(buf), 0);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "create with NULL arena rejected");

    /* NULL buffer pointer */
    err = tiku_arena_create(&arena, NULL, 32, 0);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "create with NULL buffer rejected");

    /* Alloc from NULL arena */
    p = tiku_arena_alloc(NULL, 4);
    TEST_ASSERT(p == NULL, "alloc from NULL arena returns NULL");

    /* Alloc zero bytes */
    tiku_arena_create(&arena, buf, sizeof(buf), 7);
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

static void test_secure_reset(void)
{
    uint8_t buf[32];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;
    int all_zero;
    uint16_t i;
    void *p;

    printf("\n--- Test: Secure Reset ---\n");

    tiku_arena_create(&arena, buf, sizeof(buf), 8);

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
    for (i = 0; i < sizeof(buf); i++) {
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

static void test_two_arenas(void)
{
    uint8_t buf_a[32];
    uint8_t buf_b[64];
    tiku_arena_t arena_a, arena_b;
    tiku_mem_stats_t stats_a, stats_b;
    void *pa, *pb;

    printf("\n--- Test: Two Independent Arenas ---\n");

    tiku_arena_create(&arena_a, buf_a, sizeof(buf_a), 10);
    tiku_arena_create(&arena_b, buf_b, sizeof(buf_b), 20);

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

/*---------------------------------------------------------------------------*/
/* MAIN                                                                      */
/*---------------------------------------------------------------------------*/

int main(void)
{
    printf("=== TikuOS Arena Allocator Tests ===\n");

    test_create_and_stats();
    test_basic_alloc();
    test_alignment();
    test_arena_full();
    test_reset();
    test_peak_tracking();
    test_invalid_inputs();
    test_secure_reset();
    test_two_arenas();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
