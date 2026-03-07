/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem.c - Arena allocator implementation
 *
 * Implements the arena (bump-pointer) allocator for fragmentation-free
 * memory management on microcontrollers with small SRAM.
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
 * @brief Round a size up to the platform's required alignment
 *
 * Uses TIKU_MEM_ARCH_ALIGNMENT (provided by the memory HAL) so the
 * same code works across 16-bit, 32-bit, and 64-bit targets.
 *
 * The standard power-of-two round-up formula:
 *   (size + (align - 1)) & ~(align - 1)
 *
 * Example (alignment = 4): 3 -> 4, 4 -> 4, 5 -> 8, 0 -> 0
 *
 * @param size  Raw size in bytes
 * @return Size rounded up to TIKU_MEM_ARCH_ALIGNMENT boundary
 */
static tiku_mem_arch_size_t align_up(tiku_mem_arch_size_t size)
{
    const tiku_mem_arch_size_t mask = TIKU_MEM_ARCH_ALIGNMENT - 1U;
    return (size + mask) & ~mask;
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
                                 tiku_mem_arch_size_t size, uint8_t id)
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
 * Why alignment:
 *   The target architecture may require word-aligned access. Unaligned
 *   access can trigger a bus fault or incur a performance penalty.
 *   Aligning every allocation to TIKU_MEM_ARCH_ALIGNMENT bytes ensures
 *   any returned pointer is safe for native word or struct access.
 *
 * @param arena    Arena to allocate from (must be active)
 * @param size     Bytes requested (must be > 0)
 * @return Aligned pointer, or NULL on failure
 */
void *tiku_arena_alloc(tiku_arena_t *arena, tiku_mem_arch_size_t size)
{
    tiku_mem_arch_size_t aligned;
    void *ptr;

    if (arena == NULL || !arena->active || size == 0) {
        return NULL;
    }

    aligned = align_up(size);

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
 * Delegates to tiku_mem_arch_secure_wipe() for a platform-optimized
 * zeroing implementation, then resets the arena state. The arch layer
 * uses a volatile pointer (or equivalent) to ensure the compiler
 * cannot optimize away the zeroing.
 *
 * This is O(n) in buffer size — use tiku_arena_reset() for the fast
 * path and this function only when the arena held cryptographic keys,
 * nonces, or other secrets that must not survive in memory.
 *
 * @param arena    Arena to securely reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if arena is NULL
 */
tiku_mem_err_t tiku_arena_secure_reset(tiku_arena_t *arena)
{
    if (arena == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Delegate to the arch layer for a platform-optimized secure wipe. */
    tiku_mem_arch_secure_wipe(arena->buf, arena->capacity);

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
 * Called during boot from tiku_boot_init_memory(). Activates MPU
 * NVM write-protection first — this is the earliest point we can
 * lock down NVM, before any other subsystem has a chance to run.
 * Then performs platform-specific memory hardware setup via the HAL.
 */
void tiku_mem_init(void)
{
    /* Activate NVM write-protection before anything else */
    tiku_mpu_init();

    tiku_mem_arch_init();
}
