/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem.c - Memory management implementation
 *
 * Implements the arena (bump-pointer) allocator for fragmentation-free
 * memory management on microcontrollers with small SRAM. Additional
 * allocator types will be added to this file as the module grows.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Round a size up to 2-byte alignment
 *
 * MSP430 is a 16-bit architecture. Unaligned word access causes a bus
 * fault, so every allocation must start on an even address. This rounds
 * odd sizes up to the next multiple of 2.
 *
 * Example: 3 -> 4, 4 -> 4, 1 -> 2, 0 -> 0
 *
 * @param size  Raw size in bytes
 * @return Size rounded up to 2-byte boundary
 */
static uint16_t align2(uint16_t size)
{
    return (size + 1U) & ~1U;
}

/*---------------------------------------------------------------------------*/
/* ARENA FUNCTIONS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize an arena over a caller-provided buffer
 *
 * The arena does not own or allocate the buffer — the caller provides
 * a static array or a section of SRAM. This avoids any dependency on
 * a heap allocator.
 *
 * @param arena    Arena control block to initialize
 * @param buf      Pointer to the backing buffer
 * @param size     Size of the backing buffer in bytes
 * @param id       User-assigned identifier for debugging
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad arguments
 */
tiku_mem_err_t tiku_arena_create(tiku_arena_t *arena, uint8_t *buf,
                                 uint16_t size, uint8_t id)
{
    if (arena == NULL || buf == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    arena->buf      = buf;
    arena->capacity = size;
    arena->offset   = 0;
    arena->peak     = 0;
    arena->count    = 0;
    arena->id       = id;
    arena->active   = 1;

    return TIKU_MEM_OK;
}

/**
 * @brief Allocate memory from an arena (bump-pointer)
 *
 * Advances the offset by the aligned size. Tracks the peak offset
 * for lifetime high-water mark reporting.
 *
 * Why no individual free:
 *   Arenas are designed for groups of allocations with a shared lifetime.
 *   Removing individual free eliminates per-object metadata, free-list
 *   traversal, and fragmentation — all critical on small SRAM.
 *
 * Why 2-byte alignment:
 *   MSP430 requires word-aligned access for 16-bit loads and stores.
 *   An unaligned word access triggers a bus fault. Aligning every
 *   allocation to 2 bytes ensures any returned pointer is safe for
 *   uint16_t or struct access.
 *
 * @param arena    Arena to allocate from (must be active)
 * @param size     Bytes requested (must be > 0)
 * @return Aligned pointer, or NULL on failure
 */
void *tiku_arena_alloc(tiku_arena_t *arena, uint16_t size)
{
    uint16_t aligned;
    void *ptr;

    if (arena == NULL || !arena->active || size == 0) {
        return NULL;
    }

    aligned = align2(size);

    /* Check for overflow: would the new offset exceed capacity? */
    if (aligned > arena->capacity - arena->offset) {
        return NULL;
    }

    ptr = &arena->buf[arena->offset];
    arena->offset += aligned;

    /* Track lifetime high-water mark */
    if (arena->offset > arena->peak) {
        arena->peak = arena->offset;
    }

    arena->count++;

    return ptr;
}

/**
 * @brief Reset an arena, reclaiming all allocations at once
 *
 * Sets the offset back to zero so the entire buffer is available again.
 * The allocation count is also reset.
 *
 * Why memory is not zeroed:
 *   Zeroing the buffer would make reset O(n) in buffer size, which
 *   defeats the purpose of an O(1) bump-pointer allocator. Callers
 *   should not rely on arena memory being zeroed — just as malloc()
 *   does not guarantee zeroed memory.
 *
 * The peak high-water mark is intentionally preserved across resets
 * so it reflects the lifetime maximum, not just the current cycle.
 *
 * @param arena    Arena to reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if arena is NULL
 */
tiku_mem_err_t tiku_arena_reset(tiku_arena_t *arena)
{
    if (arena == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    arena->offset = 0;
    arena->count  = 0;

    return TIKU_MEM_OK;
}

/**
 * @brief Securely reset an arena, zeroing all memory before reclaiming
 *
 * Overwrites the entire backing buffer with zeros using a volatile
 * pointer, then resets the arena state. The volatile qualifier
 * ensures the compiler cannot optimize away the zeroing — without it,
 * the compiler sees that the zeroed memory is never read afterward
 * and is free to eliminate the entire loop (observed with GCC -O2 and
 * LLVM on MSP430).
 *
 * CPU-cycle cost on MSP430 (16-bit RISC, single-cycle SRAM writes):
 *   The inner loop compiles to roughly:
 *       MOV.B #0, 0(Rn)    ; 4 cycles (indexed mode)
 *       INC   Rn            ; 1 cycle
 *       CMP   Rn, Rm        ; 1 cycle
 *       JNZ   loop          ; 2 cycles (taken)
 *   Total: ~8 cycles per byte on MSP430, ~5 cycles per byte on MSP430X.
 *   For comparison, tiku_arena_reset() is ~6 cycles total (constant).
 *
 *   Concrete examples at 16 MHz MCLK:
 *     64 B arena  →   ~512 cycles →   32 us
 *    256 B arena  →  ~2048 cycles →  128 us
 *   2048 B arena  → ~16384 cycles → 1024 us  (1 ms)
 *
 *   This is the price you pay for scrubbing sensitive data. Use
 *   tiku_arena_reset() for the fast path and this function only
 *   when the arena held cryptographic keys, nonces, or other
 *   secrets that must not survive in memory.
 *
 * @param arena    Arena to securely reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if arena is NULL
 */
tiku_mem_err_t tiku_arena_secure_reset(tiku_arena_t *arena)
{
    volatile uint8_t *p;
    uint16_t i;

    if (arena == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    /*
     * Zero the entire buffer through a volatile pointer.
     * The volatile cast prevents the compiler from eliding this loop
     * even though the memory is not read afterward. This is the
     * standard portable technique for secure memset (see also
     * C11 memset_s, explicit_bzero on BSD/Linux).
     */
    p = (volatile uint8_t *)arena->buf;
    for (i = 0; i < arena->capacity; i++) {
        p[i] = 0;
    }

    arena->offset = 0;
    arena->count  = 0;

    return TIKU_MEM_OK;
}

/**
 * @brief Fill a stats struct with the arena's current state
 *
 * @param arena    Arena to query
 * @param stats    Output structure
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if either is NULL
 */
tiku_mem_err_t tiku_arena_stats(const tiku_arena_t *arena,
                                tiku_mem_stats_t *stats)
{
    if (arena == NULL || stats == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    stats->total_bytes = arena->capacity;
    stats->used_bytes  = arena->offset;
    stats->peak_bytes  = arena->peak;
    stats->alloc_count = arena->count;

    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* MODULE INIT                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the memory management module
 *
 * Currently a no-op. This is the entry point that tiku_cpu_full_init()
 * (or equivalent) will call. As additional allocator subsystems (pool,
 * slab, etc.) are added, their initialization will go here.
 */
void tiku_mem_init(void)
{
    /* Nothing to do yet — arena initialization is per-instance via
     * tiku_arena_create(). Future subsystems will be initialized here. */
}
