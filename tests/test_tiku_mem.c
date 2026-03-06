/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_tiku_mem.c - Memory module tests (host-compilable)
 *
 * Standalone test program for the memory management module (arena
 * allocator + persistent FRAM key-value store). Compiles and runs
 * on a host machine with GCC — no MSP430 toolchain needed.
 *
 * Host mode (no MSP430 toolchain needed — HAL provides fallback
 * defaults: 4-byte alignment, uint32_t sizes):
 *
 *   gcc -std=c99 -Wall -Wextra -I. \
 *       tests/test_tiku_mem.c \
 *       kernel/memory/tiku_mem.c \
 *       kernel/memory/tiku_persist.c \
 *       kernel/memory/tiku_mpu.c \
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

#endif /* !PLATFORM_MSP430 */

/*---------------------------------------------------------------------------*/
/* TEST HELPERS                                                              */
/*---------------------------------------------------------------------------*/

/** Round @p x up to the platform alignment — mirrors the kernel's align_up */
#define TEST_ALIGN_UP(x) \
    (((x) + (TIKU_MEM_ARCH_ALIGNMENT - 1U)) & ~(TIKU_MEM_ARCH_ALIGNMENT - 1U))

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

void test_mem_create_and_stats(void)
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

void test_mem_basic_alloc(void)
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
/* TEST 3: ALIGNMENT OF ODD-SIZED REQUESTS                                   */
/*---------------------------------------------------------------------------*/

void test_mem_alignment(void)
{
    uint8_t buf[64];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    void *p1, *p2, *p3;
    const unsigned int A = TIKU_MEM_ARCH_ALIGNMENT;

    /* Compute expected aligned sizes for the three requests */
    const tiku_mem_arch_size_t a1 = TEST_ALIGN_UP(3);  /* 3 -> 4 */
    const tiku_mem_arch_size_t a2 = TEST_ALIGN_UP(1);  /* 1 -> A */
    const tiku_mem_arch_size_t a3 = TEST_ALIGN_UP(5);  /* 5 -> next multiple of A */

    printf("\n--- Test: %u-Byte Alignment ---\n", A);

    tiku_arena_create(&arena, buf, sizeof(buf), 3);

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
    uint8_t buf[4 * TIKU_MEM_ARCH_ALIGNMENT];
    tiku_arena_t arena;
    void *p1, *p2, *p3;

    printf("\n--- Test: Arena Full ---\n");

    tiku_arena_create(&arena, buf, sizeof(buf), 4);

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
    uint8_t buf[64];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;

    printf("\n--- Test: Reset ---\n");

    tiku_arena_create(&arena, buf, sizeof(buf), 5);

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
    uint8_t buf[64];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;

    printf("\n--- Test: Peak Tracking Across Resets ---\n");

    tiku_arena_create(&arena, buf, sizeof(buf), 6);

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

void test_mem_secure_reset(void)
{
    uint8_t buf[32];
    tiku_arena_t arena;
    tiku_mem_stats_t stats;
    tiku_mem_err_t err;
    int all_zero;
    unsigned int i;
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

void test_mem_two_arenas(void)
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
/* TEST 10: PERSIST INIT ON ZEROED STORE                                     */
/*---------------------------------------------------------------------------*/

void test_persist_init_zeroed(void)
{
    tiku_persist_store_t store;
    tiku_mem_err_t err;

    printf("\n--- Test: Persist Init on Zeroed Store ---\n");

    memset(&store, 0, sizeof(store));
    err = tiku_persist_init(&store);
    TEST_ASSERT(err == TIKU_MEM_OK, "persist_init returns OK");
    TEST_ASSERT(store.count == 0, "count is 0 on zeroed store");

    /* NULL store rejected */
    err = tiku_persist_init(NULL);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "persist_init NULL rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST 11: REGISTER INCREMENTS COUNT                                        */
/*---------------------------------------------------------------------------*/

void test_persist_register_and_count(void)
{
    tiku_persist_store_t store;
    uint8_t fram_buf[32];

    printf("\n--- Test: Persist Register and Count ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    TEST_ASSERT(store.count == 0, "count is 0 before register");

    tiku_persist_register(&store, "cfg", fram_buf, sizeof(fram_buf));
    TEST_ASSERT(store.count == 1, "count is 1 after first register");

    /* NULL args rejected */
    TEST_ASSERT(tiku_persist_register(NULL, "x", fram_buf, 4)
                == TIKU_MEM_ERR_INVALID, "NULL store rejected");
    TEST_ASSERT(tiku_persist_register(&store, NULL, fram_buf, 4)
                == TIKU_MEM_ERR_INVALID, "NULL key rejected");
    TEST_ASSERT(tiku_persist_register(&store, "x", NULL, 4)
                == TIKU_MEM_ERR_INVALID, "NULL buffer rejected");
    TEST_ASSERT(tiku_persist_register(&store, "x", fram_buf, 0)
                == TIKU_MEM_ERR_INVALID, "zero capacity rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST 12: WRITE THEN READ BACK                                             */
/*---------------------------------------------------------------------------*/

void test_persist_write_read(void)
{
    tiku_persist_store_t store;
    uint8_t fram_buf[32];
    uint8_t read_buf[32];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};

    printf("\n--- Test: Persist Write Then Read ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    tiku_persist_register(&store, "cal", fram_buf, sizeof(fram_buf));

    err = tiku_persist_write(&store, "cal", data, sizeof(data));
    TEST_ASSERT(err == TIKU_MEM_OK, "write returns OK");

    memset(read_buf, 0, sizeof(read_buf));
    err = tiku_persist_read(&store, "cal", read_buf, sizeof(read_buf),
                            &out_len);
    TEST_ASSERT(err == TIKU_MEM_OK, "read returns OK");
    TEST_ASSERT(out_len == sizeof(data), "read length matches write length");
    TEST_ASSERT(memcmp(read_buf, data, sizeof(data)) == 0,
                "read data matches written data");
}

/*---------------------------------------------------------------------------*/
/* TEST 13: READ WITH TOO-SMALL BUFFER                                       */
/*---------------------------------------------------------------------------*/

void test_persist_read_small_buffer(void)
{
    tiku_persist_store_t store;
    uint8_t fram_buf[32];
    uint8_t tiny_buf[2];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};

    printf("\n--- Test: Persist Read Small Buffer ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    tiku_persist_register(&store, "big", fram_buf, sizeof(fram_buf));
    tiku_persist_write(&store, "big", data, sizeof(data));

    out_len = 0;
    err = tiku_persist_read(&store, "big", tiny_buf, sizeof(tiny_buf),
                            &out_len);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOMEM, "read with small buffer returns ERR_NOMEM");
    TEST_ASSERT(out_len == sizeof(data), "out_len reports required size");
}

/*---------------------------------------------------------------------------*/
/* TEST 14: WRITE EXCEEDING CAPACITY                                         */
/*---------------------------------------------------------------------------*/

void test_persist_write_exceeds_capacity(void)
{
    tiku_persist_store_t store;
    uint8_t fram_buf[4];
    const uint8_t big_data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    tiku_mem_err_t err;

    printf("\n--- Test: Persist Write Exceeds Capacity ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    tiku_persist_register(&store, "tiny", fram_buf, sizeof(fram_buf));

    err = tiku_persist_write(&store, "tiny", big_data, sizeof(big_data));
    TEST_ASSERT(err == TIKU_MEM_ERR_NOMEM,
                "write exceeding capacity returns ERR_NOMEM");
}

/*---------------------------------------------------------------------------*/
/* TEST 15: READ NON-EXISTENT KEY                                            */
/*---------------------------------------------------------------------------*/

void test_persist_read_not_found(void)
{
    tiku_persist_store_t store;
    uint8_t buf[16];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;

    printf("\n--- Test: Persist Read Not Found ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);

    err = tiku_persist_read(&store, "nope", buf, sizeof(buf), &out_len);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "read non-existent key returns ERR_NOT_FOUND");
}

/*---------------------------------------------------------------------------*/
/* TEST 16: DELETE ENTRY                                                      */
/*---------------------------------------------------------------------------*/

void test_persist_delete(void)
{
    tiku_persist_store_t store;
    uint8_t fram_buf[16];
    uint8_t read_buf[16];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {0xAA, 0xBB};

    printf("\n--- Test: Persist Delete ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    tiku_persist_register(&store, "del", fram_buf, sizeof(fram_buf));
    tiku_persist_write(&store, "del", data, sizeof(data));

    err = tiku_persist_delete(&store, "del");
    TEST_ASSERT(err == TIKU_MEM_OK, "delete returns OK");
    TEST_ASSERT(store.count == 0, "count decremented after delete");

    err = tiku_persist_read(&store, "del", read_buf, sizeof(read_buf),
                            &out_len);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "read after delete returns ERR_NOT_FOUND");

    /* Delete non-existent key */
    err = tiku_persist_delete(&store, "gone");
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "delete non-existent key returns ERR_NOT_FOUND");
}

/*---------------------------------------------------------------------------*/
/* TEST 17: STORE FULL                                                        */
/*---------------------------------------------------------------------------*/

void test_persist_full(void)
{
    tiku_persist_store_t store;
    uint8_t fram_bufs[TIKU_PERSIST_MAX_ENTRIES][4];
    uint8_t extra_buf[4];
    char key[TIKU_PERSIST_MAX_KEY_LEN];
    tiku_mem_err_t err;
    int i;

    printf("\n--- Test: Persist Store Full ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);

    /* Fill all slots */
    for (i = 0; i < TIKU_PERSIST_MAX_ENTRIES; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        err = tiku_persist_register(&store, key, fram_bufs[i],
                                    sizeof(fram_bufs[i]));
        TEST_ASSERT(err == TIKU_MEM_OK, "register slot succeeds");
    }

    TEST_ASSERT(store.count == TIKU_PERSIST_MAX_ENTRIES,
                "count equals max entries");

    /* One more should fail */
    err = tiku_persist_register(&store, "extra", extra_buf,
                                sizeof(extra_buf));
    TEST_ASSERT(err == TIKU_MEM_ERR_FULL,
                "register beyond max returns ERR_FULL");
}

/*---------------------------------------------------------------------------*/
/* TEST 18: REBOOT SURVIVAL (RE-INIT PRESERVES VALID ENTRIES)                */
/*---------------------------------------------------------------------------*/

void test_persist_reboot_survival(void)
{
    tiku_persist_store_t store;
    uint8_t fram_buf[16];
    uint8_t read_buf[16];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {0x42, 0x43, 0x44};

    printf("\n--- Test: Persist Reboot Survival ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);

    tiku_persist_register(&store, "boot", fram_buf, sizeof(fram_buf));
    tiku_persist_write(&store, "boot", data, sizeof(data));

    /* Simulate reboot: re-init the same store (entries have magic set) */
    tiku_persist_init(&store);

    TEST_ASSERT(store.count == 1, "count is 1 after re-init");

    /* Data should still be readable */
    memset(read_buf, 0, sizeof(read_buf));
    err = tiku_persist_read(&store, "boot", read_buf, sizeof(read_buf),
                            &out_len);
    TEST_ASSERT(err == TIKU_MEM_OK, "read after re-init returns OK");
    TEST_ASSERT(out_len == sizeof(data),
                "data length preserved after re-init");
    TEST_ASSERT(memcmp(read_buf, data, sizeof(data)) == 0,
                "data intact after re-init (reboot survival)");
}

/*---------------------------------------------------------------------------*/
/* TEST 19: WEAR CHECK                                                        */
/*---------------------------------------------------------------------------*/

void test_persist_wear_check(void)
{
    tiku_persist_store_t store;
    uint8_t fram_buf[8];
    uint32_t wc;
    int result;
    tiku_persist_entry_t *entry;

    printf("\n--- Test: Persist Wear Check ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    tiku_persist_register(&store, "wear", fram_buf, sizeof(fram_buf));

    result = tiku_persist_wear_check(&store, "wear", &wc);
    TEST_ASSERT(result == 0, "wear check returns 0 initially");
    TEST_ASSERT(wc == 0, "write_count is 0 initially");

    /* Manually set write_count above threshold */
    entry = &store.entries[0];
    entry->write_count = TIKU_PERSIST_WEAR_THRESHOLD + 1;

    result = tiku_persist_wear_check(&store, "wear", &wc);
    TEST_ASSERT(result == 1, "wear check returns 1 above threshold");
    TEST_ASSERT(wc == TIKU_PERSIST_WEAR_THRESHOLD + 1,
                "write_count matches set value");

    /* Non-existent key */
    result = tiku_persist_wear_check(&store, "nope", &wc);
    TEST_ASSERT(result == TIKU_MEM_ERR_NOT_FOUND,
                "wear check non-existent key returns ERR_NOT_FOUND");
}

/*---------------------------------------------------------------------------*/
/* TEST 20: REGISTER SAME KEY TWICE                                           */
/*---------------------------------------------------------------------------*/

void test_persist_register_twice(void)
{
    tiku_persist_store_t store;
    uint8_t fram_buf1[16];
    uint8_t fram_buf2[32];
    uint8_t read_buf[32];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {0x11, 0x22, 0x33};

    printf("\n--- Test: Persist Register Same Key Twice ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);

    tiku_persist_register(&store, "dup", fram_buf1, sizeof(fram_buf1));
    tiku_persist_write(&store, "dup", data, sizeof(data));

    /* Re-register with different buffer — should update pointer, keep data */
    err = tiku_persist_register(&store, "dup", fram_buf2, sizeof(fram_buf2));
    TEST_ASSERT(err == TIKU_MEM_OK, "re-register same key returns OK");
    TEST_ASSERT(store.count == 1, "count stays 1 (no duplicate slot)");

    /* Data should still be readable (value_len preserved) */
    memset(read_buf, 0, sizeof(read_buf));
    err = tiku_persist_read(&store, "dup", read_buf, sizeof(read_buf),
                            &out_len);
    TEST_ASSERT(err == TIKU_MEM_OK, "read after re-register returns OK");
    TEST_ASSERT(out_len == sizeof(data),
                "value_len preserved after re-register");
}

/*---------------------------------------------------------------------------*/
/* TEST 21: MPU INIT SETS DEFAULT PROTECTION                                  */
/*---------------------------------------------------------------------------*/

void test_mpu_init_defaults(void)
{
    printf("\n--- Test: MPU Init Defaults ---\n");

    tiku_mpu_init();

    /* All 3 segments should be R+X (0x5 per nybble) = 0x0555 */
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "SAM is 0x0555 after init (all segments R+X, no W)");

    /* MPU should be enabled (MPUENA = 0x0001 in lower byte) */
    TEST_ASSERT((tiku_mpu_arch_get_ctl() & 0x0001) != 0,
                "CTL has enable bit set after init");
}

/*---------------------------------------------------------------------------*/
/* TEST 22: MPU UNLOCK / LOCK FRAM                                            */
/*---------------------------------------------------------------------------*/

void test_mpu_unlock_lock(void)
{
    uint16_t saved;

    printf("\n--- Test: MPU Unlock / Lock ---\n");

    tiku_mpu_init();

    saved = tiku_mpu_unlock_fram();
    TEST_ASSERT(saved == 0x0555,
                "unlock returns previous SAM (0x0555)");

    /* After unlock, write bits should be set: 0x0555 | 0x0222 = 0x0777 */
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0777,
                "SAM has write bits after unlock (0x0777)");

    tiku_mpu_lock_fram(saved);
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "SAM restored to 0x0555 after lock");
}

/*---------------------------------------------------------------------------*/
/* TEST 23: MPU SET PERMISSIONS ON ONE SEGMENT                                */
/*---------------------------------------------------------------------------*/

void test_mpu_set_permissions(void)
{
    uint16_t sam;

    printf("\n--- Test: MPU Set Permissions ---\n");

    tiku_mpu_init();
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555, "baseline is 0x0555");

    /* Set segment 3 (bits [11:8]) to RD_WR (0x03) */
    tiku_mpu_set_permissions(TIKU_MPU_SEG3, TIKU_MPU_RD_WR);

    sam = tiku_mpu_arch_get_sam();
    /* Segment 1 and 2 unchanged (0x55), segment 3 now 0x3 */
    TEST_ASSERT((sam & 0x000F) == 0x0005,
                "segment 1 unchanged (R+X)");
    TEST_ASSERT((sam & 0x00F0) == 0x0050,
                "segment 2 unchanged (R+X)");
    TEST_ASSERT((sam & 0x0F00) == 0x0300,
                "segment 3 set to RD_WR (0x3)");
}

/*---------------------------------------------------------------------------*/
/* TEST 24: MPU SCOPED WRITE                                                  */
/*---------------------------------------------------------------------------*/

/* Context for the scoped-write test callback */
typedef struct {
    int called;
    uint16_t sam_during;
} scoped_write_ctx_t;

static void scoped_write_cb(void *arg)
{
    scoped_write_ctx_t *ctx = (scoped_write_ctx_t *)arg;
    ctx->called = 1;
    ctx->sam_during = tiku_mpu_arch_get_sam();
}

void test_mpu_scoped_write(void)
{
    scoped_write_ctx_t ctx;

    printf("\n--- Test: MPU Scoped Write ---\n");

    tiku_mpu_init();

    ctx.called = 0;
    ctx.sam_during = 0;

    tiku_mpu_scoped_write(scoped_write_cb, &ctx);

    TEST_ASSERT(ctx.called == 1,
                "callback was invoked");
    TEST_ASSERT((ctx.sam_during & 0x0222) == 0x0222,
                "SAM had write bits during callback");
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "SAM locked again after scoped_write");
}

/*---------------------------------------------------------------------------*/
/* TEST 25: MPU LOCK / UNLOCK IDEMPOTENCY                                     */
/*---------------------------------------------------------------------------*/

void test_mpu_idempotent(void)
{
    uint16_t saved1, saved2;

    printf("\n--- Test: MPU Lock/Unlock Idempotency ---\n");

    tiku_mpu_init();

    /* Lock when already locked — state should not change */
    tiku_mpu_lock_fram(0x0555);
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "lock when already locked keeps 0x0555");

    /* Double unlock — second call should return the already-unlocked state */
    saved1 = tiku_mpu_unlock_fram();
    TEST_ASSERT(saved1 == 0x0555, "first unlock returns 0x0555");

    saved2 = tiku_mpu_unlock_fram();
    TEST_ASSERT(saved2 == 0x0777,
                "second unlock returns 0x0777 (already unlocked)");
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0777,
                "SAM still 0x0777 after double unlock");

    /* Restoring with saved1 should relock properly */
    tiku_mpu_lock_fram(saved1);
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "lock with original saved state restores 0x0555");
}

/*---------------------------------------------------------------------------*/
/* MAIN (host-only standalone test runner)                                   */
/*---------------------------------------------------------------------------*/

#ifndef PLATFORM_MSP430
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
