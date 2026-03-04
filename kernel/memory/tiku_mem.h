/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem.h - Memory management module
 *
 * TikuOS memory management for microcontrollers with small SRAM (2-8 KB).
 * The first allocator is the arena (bump-pointer) allocator, designed for
 * groups of allocations that share a lifetime. More allocator types (pool,
 * slab, etc.) will be added to this module over time.
 *
 * Why arenas:
 *   malloc/free on small SRAM leads to fragmentation that is fatal on
 *   microcontrollers with no MMU. An arena allocates by advancing a
 *   pointer — O(1) with zero per-object metadata — and frees everything
 *   at once by resetting the pointer. Ideal for temporary buffers during
 *   packet processing, sensor data batching, or any scenario where a
 *   group of allocations is used and discarded together.
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

#ifndef TIKU_MEM_H_
#define TIKU_MEM_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>
#include "hal/tiku_mem_hal.h"

/*---------------------------------------------------------------------------*/
/* ERROR CODES                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Memory subsystem error codes
 *
 * Shared by all allocator types in the memory module.
 */
typedef enum {
    TIKU_MEM_OK         = 0,    /**< Operation succeeded */
    TIKU_MEM_ERR_INVALID = -1,  /**< Invalid argument (NULL pointer, etc.) */
    TIKU_MEM_ERR_NOMEM  = -2    /**< Out of memory */
} tiku_mem_err_t;

/*---------------------------------------------------------------------------*/
/* STATISTICS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Memory usage statistics
 *
 * Snapshot of an allocator's current state. All sizes are in bytes.
 * The size type is provided by the memory HAL for the target platform.
 */
typedef struct {
    tiku_mem_arch_size_t total_bytes;  /**< Total capacity of the backing buffer */
    tiku_mem_arch_size_t used_bytes;   /**< Currently allocated bytes */
    tiku_mem_arch_size_t peak_bytes;   /**< High-water mark (lifetime maximum) */
    tiku_mem_arch_size_t alloc_count;  /**< Number of successful allocations */
} tiku_mem_stats_t;

/*---------------------------------------------------------------------------*/
/* ARENA ALLOCATOR                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Arena (bump-pointer) allocator control block
 *
 * An arena manages a contiguous caller-provided buffer. Each allocation
 * advances an offset forward. There is no individual free — all
 * allocations are discarded at once via tiku_arena_reset().
 *
 * Fields:
 *   buf       – pointer to the start of the backing buffer
 *   capacity  – total size of the backing buffer in bytes
 *   offset    – current allocation position (next free byte)
 *   peak      – highest offset ever reached (survives reset)
 *   count     – number of successful allocations since last reset
 *   id        – user-assigned identifier for debugging
 *   active    – non-zero if the arena has been initialized
 */
typedef struct {
    uint8_t              *buf;       /**< Backing buffer (caller-provided) */
    tiku_mem_arch_size_t  capacity;  /**< Buffer size in bytes */
    tiku_mem_arch_size_t  offset;    /**< Current bump-pointer position */
    tiku_mem_arch_size_t  peak;      /**< Lifetime high-water mark */
    tiku_mem_arch_size_t  count;     /**< Allocations since last reset */
    uint8_t               id;        /**< Arena identifier for debugging */
    uint8_t               active;    /**< Non-zero if initialized */
} tiku_arena_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — ARENA                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize an arena over a caller-provided buffer
 *
 * Sets up the arena control block. The buffer must be provided by the
 * caller (typically a static array). The arena does not own the buffer.
 *
 * @param arena    Arena control block to initialize
 * @param buf      Pointer to the backing buffer
 * @param size     Size of the backing buffer in bytes
 * @param id       User-assigned identifier (0-255)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if arena or buf is NULL
 */
tiku_mem_err_t tiku_arena_create(tiku_arena_t *arena, uint8_t *buf,
                                 tiku_mem_arch_size_t size, uint8_t id);

/**
 * @brief Allocate memory from an arena
 *
 * Bump-pointer allocation. The returned pointer is aligned to the
 * platform's native word boundary (TIKU_MEM_ARCH_ALIGNMENT). Requests
 * that are not a multiple of the alignment are rounded up internally.
 *
 * There is no individual free. Use tiku_arena_reset() to reclaim all
 * allocations at once.
 *
 * @param arena    Arena to allocate from
 * @param size     Number of bytes requested (must be > 0)
 * @return Pointer to the allocated memory, or NULL if the arena is full
 *         or the arguments are invalid
 */
void *tiku_arena_alloc(tiku_arena_t *arena, tiku_mem_arch_size_t size);

/**
 * @brief Reset an arena, reclaiming all allocations
 *
 * Sets the offset back to zero. Does not zero the buffer memory —
 * this keeps reset O(1) regardless of arena size. The peak high-water
 * mark is preserved across resets for lifetime tracking.
 *
 * @param arena    Arena to reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if arena is NULL
 */
tiku_mem_err_t tiku_arena_reset(tiku_arena_t *arena);

/**
 * @brief Securely reset an arena, zeroing all memory before reclaiming
 *
 * Same as tiku_arena_reset() but first overwrites the entire buffer
 * with zeros. Use this when the arena held sensitive data such as
 * cryptographic keys, nonces, or credentials that must not linger
 * in SRAM (physical probing, memory-dump bugs) or in FRAM (persists
 * across reboots).
 *
 * A volatile pointer is used for the zeroing loop to prevent the
 * compiler from optimizing out the memset — a known problem in
 * security-sensitive code (the compiler sees that the memory is never
 * read after zeroing and may elide the entire operation).
 *
 * CPU-cycle penalty vs tiku_arena_reset():
 *   tiku_arena_reset()        — O(1), ~6 cycles regardless of size.
 *   tiku_arena_secure_reset() — O(n), approximately 5 cycles per byte
 *       on MSP430 (MOV.B + INC + CMP + JNZ loop body). For typical
 *       arena sizes:
 *         64 B   ~  320 cycles  (  20 us @ 16 MHz)
 *        256 B   ~ 1280 cycles  (  80 us @ 16 MHz)
 *       2048 B   ~10240 cycles  ( 640 us @ 16 MHz)
 *       Only use when the security benefit justifies the cost.
 *
 * @param arena    Arena to securely reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if arena is NULL
 */
tiku_mem_err_t tiku_arena_secure_reset(tiku_arena_t *arena);

/**
 * @brief Get current statistics for an arena
 *
 * Fills a tiku_mem_stats_t with a snapshot of the arena's state.
 *
 * @param arena    Arena to query
 * @param stats    Output structure (caller-provided)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if either pointer is NULL
 */
tiku_mem_err_t tiku_arena_stats(const tiku_arena_t *arena,
                                tiku_mem_stats_t *stats);

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — MODULE                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the memory management module
 *
 * Entry point for the memory subsystem. Currently a no-op — will
 * initialize additional allocator subsystems as they are added.
 */
void tiku_mem_init(void);

#endif /* TIKU_MEM_H_ */
