/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pool.c - Fixed-size block pool allocator implementation
 *
 * Implements a pool allocator that manages equal-sized blocks from a
 * contiguous caller-provided buffer. An embedded freelist chains free
 * blocks together — each free block stores a pointer to the next free
 * block inside its own memory, so there is zero metadata overhead.
 *
 * Why a pool allocator alongside the arena:
 *   The arena is ideal when allocations share a common lifetime and are
 *   freed together. But many embedded patterns need individual alloc/free
 *   of fixed-size objects — packet buffers, message slots, sensor sample
 *   records. A pool provides O(1) alloc and O(1) free with zero
 *   fragmentation, because every block is the same size.
 *
 * Why an embedded freelist:
 *   Each free block is large enough to hold at least one pointer. The
 *   first sizeof(void *) bytes of a free block store a pointer to the
 *   next free block. When the block is allocated, those bytes are
 *   returned to the caller — zero per-block metadata. When freed, the
 *   pointer is written back. This is the classic freelist technique used
 *   in kernels and allocators where per-object overhead must be zero.
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
 * @param size  Raw size in bytes
 * @return Size rounded up to TIKU_MEM_ARCH_ALIGNMENT boundary
 */
static tiku_mem_arch_size_t align_up(tiku_mem_arch_size_t size)
{
    const tiku_mem_arch_size_t mask = TIKU_MEM_ARCH_ALIGNMENT - 1U;
    return (size + mask) & ~mask;
}

/**
 * @brief Minimum block size for the embedded freelist
 *
 * Each free block must be large enough to hold a pointer to the next
 * free block. On MSP430, sizeof(void *) is 2 bytes. On 32-bit hosts,
 * it is 4 bytes. The minimum is the larger of sizeof(void *) and the
 * platform alignment, then rounded up to alignment.
 *
 * @return Minimum aligned block size in bytes
 */
static tiku_mem_arch_size_t min_block_size(void)
{
    tiku_mem_arch_size_t ptr_size = (tiku_mem_arch_size_t)sizeof(void *);

    if (TIKU_MEM_ARCH_ALIGNMENT > ptr_size) {
        ptr_size = TIKU_MEM_ARCH_ALIGNMENT;
    }

    return align_up(ptr_size);
}

/*
 * Why pointer arithmetic uses uint8_t *:
 *   Struct padding and pointer size vary across platforms. By casting
 *   the buffer to uint8_t * and indexing by (i * block_size), we get
 *   exact byte-offset arithmetic that works identically on 16-bit
 *   MSP430 and 32/64-bit hosts. No platform-dependent struct layout
 *   issues.
 */

/**
 * @brief Build the freelist by chaining all blocks together
 *
 * Walks the buffer from block 0 to block (count-1), writing a next
 * pointer at the start of each block. The last block's next pointer
 * is NULL, terminating the list.
 *
 * @param pool   Pool whose freelist to build
 */
static void build_freelist(tiku_pool_t *pool)
{
    tiku_mem_arch_size_t i;
    uint8_t *block;
    void **next_ptr;

    for (i = 0; i < pool->block_count - 1U; i++) {
        block    = pool->buf + (i * pool->block_size);
        next_ptr = (void **)(void *)block;
        *next_ptr = block + pool->block_size;
    }

    /* Last block terminates the list */
    block    = pool->buf + ((pool->block_count - 1U) * pool->block_size);
    next_ptr = (void **)(void *)block;
    *next_ptr = NULL;

    pool->free_head = pool->buf;
}

/*---------------------------------------------------------------------------*/
/* POOL FUNCTIONS                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a pool over a caller-provided buffer
 *
 * The pool does not own or allocate the buffer — the caller provides
 * a static array or a section of SRAM. This avoids any dependency on
 * a heap allocator.
 *
 * The block_size is aligned up to TIKU_MEM_ARCH_ALIGNMENT and clamped
 * to a minimum of sizeof(void *) (also aligned). This minimum ensures
 * that every free block can hold the embedded freelist pointer.
 *
 * Why block_size is aligned during create:
 *   Each block must start on an aligned boundary so that the returned
 *   pointer is safe for native word or struct access. Aligning the
 *   block size means every block starts at buf + i*aligned_block_size,
 *   which is aligned if buf itself is aligned (static arrays are).
 *
 * @param pool         Pool control block to initialize
 * @param buf          Pointer to the backing buffer
 * @param block_size   Requested size of each block in bytes
 * @param block_count  Number of blocks
 * @param id           User-assigned identifier for debugging
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad arguments
 */
tiku_mem_err_t tiku_pool_create(tiku_pool_t *pool, uint8_t *buf,
                                 tiku_mem_arch_size_t block_size,
                                 tiku_mem_arch_size_t block_count,
                                 uint8_t id)
{
    tiku_mem_arch_size_t aligned_size;
    tiku_mem_arch_size_t min_size;

    if (pool == NULL || buf == NULL || block_count == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    aligned_size = align_up(block_size);
    min_size     = min_block_size();

    /*
     * Enforce minimum: each block must hold at least one pointer for the
     * embedded freelist. If the caller requests less, silently clamp up.
     * Returning an error would be hostile — the caller's data fits, we
     * just need room for the freelist pointer when the block is free.
     */
    if (aligned_size < min_size) {
        aligned_size = min_size;
    }

    pool->buf         = buf;
    pool->block_size  = aligned_size;
    pool->block_count = block_count;
    pool->used_count  = 0;
    pool->peak_count  = 0;
    pool->id          = id;
    pool->active      = 1;

    build_freelist(pool);

    return TIKU_MEM_OK;
}

/**
 * @brief Allocate a block from the pool
 *
 * Pops the head of the freelist. O(1) — no search, no fragmentation.
 *
 * Why there is no size parameter:
 *   Every block in the pool is the same size. The caller knows the
 *   block size (they chose it at create time). Omitting the size
 *   parameter prevents misuse and keeps the API minimal.
 *
 * @param pool   Pool to allocate from (must be active)
 * @return Pointer to the allocated block, or NULL if the pool is empty
 *         or the arguments are invalid
 */
void *tiku_pool_alloc(tiku_pool_t *pool)
{
    void *block;
    void **next_ptr;

    if (pool == NULL || !pool->active || pool->free_head == NULL) {
        return NULL;
    }

    /* Pop the head of the freelist */
    block    = pool->free_head;
    next_ptr = (void **)(void *)block;
    pool->free_head = *next_ptr;

    pool->used_count++;

    /* Track lifetime high-water mark */
    if (pool->used_count > pool->peak_count) {
        pool->peak_count = pool->used_count;
    }

    return block;
}

/**
 * @brief Return a block to the pool
 *
 * Pushes the block back onto the head of the freelist. O(1).
 *
 * Why ptr is validated:
 *   On a small MCU with no MMU, a stray free can silently corrupt the
 *   freelist, causing subsequent allocs to return wild pointers. We
 *   check that ptr falls within the pool's buffer and is aligned to a
 *   block boundary. This catches common bugs: double-free (if the
 *   block was already freed and the pointer is still in range, but at
 *   least the alignment check catches offset errors), freeing a pointer
 *   from a different pool, and freeing stack/heap pointers.
 *
 * @param pool   Pool the block belongs to
 * @param ptr    Pointer previously returned by tiku_pool_alloc
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if ptr is
 *         outside the pool or not aligned to a block boundary
 */
tiku_mem_err_t tiku_pool_free(tiku_pool_t *pool, void *ptr)
{
    uint8_t *block;
    tiku_mem_arch_size_t offset;
    void **next_ptr;

    if (pool == NULL || !pool->active || ptr == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    block = (uint8_t *)ptr;

    /* Validate: ptr must fall within the pool's buffer */
    if (block < pool->buf ||
        block >= pool->buf + (pool->block_count * pool->block_size)) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Validate: ptr must be aligned to a block boundary */
    offset = (tiku_mem_arch_size_t)(block - pool->buf);
    if (offset % pool->block_size != 0) {
        return TIKU_MEM_ERR_INVALID;
    }

#if TIKU_POOL_DEBUG
    /*
     * Poison freed block to catch use-after-free during development.
     * The first sizeof(void *) bytes are used for the freelist pointer,
     * so poison only the remaining bytes. 0xDE is a recognizable
     * pattern in hex dumps ("dead").
     */
    {
        tiku_mem_arch_size_t ptr_bytes;
        tiku_mem_arch_size_t i;

        ptr_bytes = (tiku_mem_arch_size_t)sizeof(void *);
        for (i = ptr_bytes; i < pool->block_size; i++) {
            block[i] = 0xDE;
        }
    }
#endif

    /* Push onto freelist head */
    next_ptr  = (void **)(void *)block;
    *next_ptr = pool->free_head;
    pool->free_head = block;

    pool->used_count--;

    return TIKU_MEM_OK;
}

/**
 * @brief Fill a stats struct with the pool's current state
 *
 * Maps pool fields to the shared tiku_mem_stats_t:
 *   total_bytes  = block_size * block_count
 *   used_bytes   = block_size * used_count
 *   peak_bytes   = block_size * peak_count
 *   alloc_count  = used_count
 *
 * @param pool    Pool to query
 * @param stats   Output structure
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if either is NULL
 */
tiku_mem_err_t tiku_pool_stats(const tiku_pool_t *pool,
                                tiku_mem_stats_t *stats)
{
    if (pool == NULL || stats == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    stats->total_bytes = pool->block_size * pool->block_count;
    stats->used_bytes  = pool->block_size * pool->used_count;
    stats->peak_bytes  = pool->block_size * pool->peak_count;
    stats->alloc_count = pool->used_count;

    return TIKU_MEM_OK;
}

/**
 * @brief Reset the pool, returning all blocks to the freelist
 *
 * Re-chains all blocks into the freelist and resets used_count.
 * The peak high-water mark is preserved across resets for lifetime
 * tracking.
 *
 * Why reset is O(n):
 *   Unlike the arena (which just moves a pointer), the pool must
 *   rebuild the freelist by writing a next-pointer into every block.
 *   This is O(n) in block_count. In practice, pool sizes are small
 *   (e.g. 8-32 blocks) so this is fast.
 *
 * @param pool   Pool to reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if pool is NULL
 */
tiku_mem_err_t tiku_pool_reset(tiku_pool_t *pool)
{
    if (pool == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    pool->used_count = 0;

    build_freelist(pool);

    return TIKU_MEM_OK;
}
