/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pool.c - fixed-size block pool allocator.
 *
 * Manages equal-sized blocks in a caller-provided buffer.  Free blocks chain
 * through an embedded freelist stored in their own memory, so there is no
 * metadata overhead and no fragmentation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"
#include <stddef.h>
#include <string.h>
#include "tiku_mem_internal.h"

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
    /* Saturate instead of wrapping to 0 on a near-max request (16-bit on
     * MSP430), so the caller's capacity check rejects it cleanly. */
    if (size > (tiku_mem_arch_size_t)(~(tiku_mem_arch_size_t)0 - mask)) {
        return (tiku_mem_arch_size_t)(~(tiku_mem_arch_size_t)0 & ~mask);
    }
    return (size + mask) & ~mask;
}

/**
 * @brief Minimum block size for the embedded freelist.
 *
 * A free block must hold a pointer to the next one, so the minimum is the
 * larger of a pointer and the platform alignment, rounded up.
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
 *   Struct padding and pointer size vary across platforms. Casting
 *   the buffer to uint8_t * and indexing by (i * block_size) gives
 *   exact byte-offset arithmetic that works identically on 16-bit
 *   MSP430 and 32/64-bit hosts, with no platform-dependent struct
 *   layout issues.
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
/*
 * Write one freelist "next" pointer into a block, honouring the backing.
 * An NVM-tier pool (pool->nvm) routes the word through tiku_tier_nvm_write()
 * -- the bootrom program op on MRAM, the flash program on RP2350, an in-place
 * store on FRAM -- because a direct CPU store would bus-fault on program-op
 * NVM. An SRAM pool stores directly (the hot path, unchanged).
 */
static tiku_mem_err_t pool_write_next(const tiku_pool_t *pool,
                                      void *block, void *next)
{
    if (pool->nvm) {
        return tiku_tier_nvm_write(block, &next,
                                   (tiku_mem_arch_size_t)sizeof(next));
    }
    *(void **)(void *)block = next;
    return TIKU_MEM_OK;
}

/*
 * Program-op NVM (carved MRAM / RP2350 Flash) is written a whole erase granule
 * at a time, so routing each freelist "next" pointer through pool_write_next()
 * one block at a time erases+reprograms a block's sector once PER block -- a
 * pool whose blocks share a sector erases it ~block_count times just at create.
 * Stage a run of whole blocks in SRAM, overlay every next-pointer in the run,
 * and write the run in a single tiku_tier_nvm_write(): the region backend then
 * coalesces to one erase per sector. Only program-op parts need this (and have
 * the SRAM for it); MSP430 FRAM / host write in place, so they keep the simple
 * per-block path below.
 */
#if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350)
#define TIKU_POOL_NVM_BATCH 1
#ifndef TIKU_POOL_NVM_STAGE_BYTES
#define TIKU_POOL_NVM_STAGE_BYTES 4096u   /* one RP2350 flash erase granule */
#endif
static uint8_t pool_nvm_stage[TIKU_POOL_NVM_STAGE_BYTES];

/**
 * @brief Thread a pool's free-list through NVM-backed block storage.
 *
 * The NVM path cannot rewrite a block in place without an erase per pointer, so
 * blocks at least a stage wide get a direct next-pointer write while smaller
 * ones sharing a sector are coalesced and written a staged run at a time.
 *
 * @param pool  Pool whose block_count / block_size / buf describe the region.
 * @return TIKU_MEM_OK, or an NVM-tier error on write failure (leaving a
 *         half-built free-list the caller must reject).
 */
static tiku_mem_err_t build_freelist_nvm(tiku_pool_t *pool)
{
    const tiku_mem_arch_size_t bs = pool->block_size;
    const tiku_mem_arch_size_t n  = pool->block_count;
    tiku_mem_arch_size_t i;

    /* A block at least a stage wide already owns its sector(s): a per-block
     * pointer write is one erase each, with nothing to coalesce. */
    if (bs > (tiku_mem_arch_size_t)TIKU_POOL_NVM_STAGE_BYTES) {
        for (i = 0; i < n; i++) {
            uint8_t *blk = pool->buf + (i * bs);
            void *next = (i + 1U < n) ? (void *)(blk + bs) : NULL;
            tiku_mem_err_t err =
                tiku_tier_nvm_write(blk, &next,
                                    (tiku_mem_arch_size_t)sizeof(next));
            if (err != TIKU_MEM_OK) {
                return err;     /* half-built freelist: caller rejects */
            }
        }
        return TIKU_MEM_OK;
    }

    /* Small blocks share sectors: write a run of whole blocks per call. */
    {
        const tiku_mem_arch_size_t per =
            (tiku_mem_arch_size_t)TIKU_POOL_NVM_STAGE_BYTES / bs;   /* >= 1 */
        for (i = 0; i < n; i += per) {
            tiku_mem_arch_size_t cnt  = (n - i < per) ? (n - i) : per;
            tiku_mem_arch_size_t span = cnt * bs;
            uint8_t *base = pool->buf + (i * bs);
            tiku_mem_arch_size_t j;

            /* Seed the run with its current NVM bytes so untouched block
             * payloads survive the write, then overlay each next-pointer. */
            memcpy(pool_nvm_stage, base, span);
            for (j = 0; j < cnt; j++) {
                tiku_mem_arch_size_t gi = i + j;
                void *next = (gi + 1U < n)
                             ? (void *)(pool->buf + ((gi + 1U) * bs)) : NULL;
                memcpy(pool_nvm_stage + (j * bs), &next, sizeof(next));
            }
            {
                tiku_mem_err_t err =
                    tiku_tier_nvm_write(base, pool_nvm_stage, span);
                if (err != TIKU_MEM_OK) {
                    return err;
                }
            }
        }
    }
    return TIKU_MEM_OK;
}
#else
#define TIKU_POOL_NVM_BATCH 0
#endif /* program-op NVM batch */

static tiku_mem_err_t build_freelist(tiku_pool_t *pool)
{
    tiku_mem_arch_size_t i;
    tiku_mem_err_t err;
    uint8_t *block;

#if TIKU_POOL_NVM_BATCH
    /* NVM-tier pool on program-op NVM: build the freelist a run at a time so
     * each sector is erased once, not once per block (see build_freelist_nvm). */
    if (pool->nvm) {
        err = build_freelist_nvm(pool);
        if (err != TIKU_MEM_OK) {
            return err;
        }
        pool->free_head = pool->buf;
        return TIKU_MEM_OK;
    }
#endif

    for (i = 0; i < pool->block_count - 1U; i++) {
        block = pool->buf + (i * pool->block_size);
        err = pool_write_next(pool, block, block + pool->block_size);
        if (err != TIKU_MEM_OK) {
            return err;
        }
    }

    /* Last block terminates the list */
    block = pool->buf + ((pool->block_count - 1U) * pool->block_size);
    err = pool_write_next(pool, block, NULL);
    if (err != TIKU_MEM_OK) {
        return err;
    }

    pool->free_head = pool->buf;
    return TIKU_MEM_OK;
}

/* Bounded freelist membership check that rejects a double-free.  Pool free is
 * normally O(1); the validation walk is O(n), but embedded pools are small and
 * preventing a used_count underflow / cyclic freelist is worth the bound. */
static int block_is_already_free(const tiku_pool_t *pool, const void *block)
{
    const void *cur = pool->free_head;
    tiku_mem_arch_size_t seen = 0;
    while (cur != NULL && seen < pool->block_count) {
        if (cur == block) {
            return 1;
        }
        cur = *(void * const *)(const void *)cur;
        seen++;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* POOL FUNCTIONS                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a pool without region-registry validation.
 *
 * For library code needing a pool over an embedded struct member before the
 * registry exists.  Marked SRAM tier with id 0; every other operation behaves
 * identically.
 *
 * @param pool         Pool control block to initialize
 * @param buf          Pointer to the backing buffer
 * @param block_size   Requested size of each block in bytes
 * @param block_count  Number of blocks
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad arguments
 */
static tiku_mem_err_t pool_create(tiku_pool_t *pool, uint8_t *buf,
        tiku_mem_arch_size_t block_size, tiku_mem_arch_size_t block_count,
        uint8_t id, uint8_t nvm)
{
    tiku_pool_t ready = {0};
    tiku_mem_arch_size_t stride, minimum = min_block_size();
    const tiku_mem_arch_size_t max = (tiku_mem_arch_size_t)~(tiku_mem_arch_size_t)0;
    tiku_mem_arch_size_t alignment = TIKU_MEM_ARCH_ALIGNMENT;
    tiku_mem_err_t err;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (alignment < __alignof__(void *)) alignment = __alignof__(void *);
    if (pool == NULL || buf == NULL || block_count == 0u ||
        block_size == 0u || (uintptr_t)buf % alignment != 0u) {
        return TIKU_MEM_ERR_INVALID;
    }
    if (tiku_backing_output_busy(pool)) return TIKU_MEM_ERR_BUSY;
    if (block_size < minimum) block_size = minimum;
    if (block_size > max - (alignment - 1u)) return TIKU_MEM_ERR_NOMEM;
    stride = (block_size + alignment - 1u) & ~(alignment - 1u);
    if (stride > max / block_count ||
        stride * block_count > UINTPTR_MAX - (uintptr_t)buf) {
        return TIKU_MEM_ERR_NOMEM;
    }
    ready.buf = buf;
    ready.block_size = stride;
    ready.block_count = block_count;
    ready.id = id;
    ready.active = 1;
    ready.nvm = nvm;
    ready.tier = nvm ? TIKU_MEM_NVM : TIKU_MEM_SRAM;
    err = build_freelist(&ready);
    if (err == TIKU_MEM_OK) *pool = ready;
    return err;
}

tiku_mem_err_t tiku_pool_create_raw(tiku_pool_t *pool, uint8_t *buf,
        tiku_mem_arch_size_t block_size, tiku_mem_arch_size_t block_count)
{
    return pool_create(pool, buf, block_size, block_count, 0, 0);
}

tiku_mem_err_t tiku_pool_create(tiku_pool_t *pool, uint8_t *buf,
        tiku_mem_arch_size_t block_size, tiku_mem_arch_size_t block_count,
        uint8_t id)
{
    return pool_create(pool, buf, block_size, block_count, id, 0);
}

tiku_mem_err_t tiku_pool_create_nvm(tiku_pool_t *pool, uint8_t *buf,
        tiku_mem_arch_size_t block_size, tiku_mem_arch_size_t block_count,
        uint8_t id)
{
    return pool_create(pool, buf, block_size, block_count, id, 1);
}

static int pool_valid(const tiku_pool_t *pool)
{
    return pool != NULL && pool->active && pool->buf != NULL &&
           pool->block_size != 0u && pool->block_count != 0u &&
           pool->used_count <= pool->block_count &&
           tiku_backing_check(pool, pool->backing, TIKU_BACKING_POOL);
}

tiku_mem_err_t tiku_pool_destroy(tiku_pool_t *pool)
{
    tiku_mem_err_t err;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!pool_valid(pool)) return TIKU_MEM_ERR_INVALID;
    if (pool->used_count != 0u) return TIKU_MEM_ERR_BUSY;
    if (pool->backing.slot_plus_one != 0u) {
        if (tiku_backing_release == NULL) return TIKU_MEM_ERR_INVALID;
        err = tiku_backing_release(pool, pool->backing, TIKU_BACKING_POOL);
        if (err != TIKU_MEM_OK) return err;
    }
    *pool = (tiku_pool_t){0};
    return TIKU_MEM_OK;
}

/**
 * @brief Allocate a block from the pool.
 *
 * Pops the freelist head in O(1), with no search and no fragmentation.  There
 * is no size parameter because every block is the same size and the caller
 * chose it at create time.
 *
 * @param pool   Pool to allocate from (must be active)
 * @return Pointer to the allocated block, or NULL if the pool is empty
 *         or the arguments are invalid
 */
void *tiku_pool_alloc(tiku_pool_t *pool)
{
    TIKU_MEM_KERNEL_ONLY(NULL);
    void *block;
    void **next_ptr;

    if (!pool_valid(pool) || pool->reset_failed || !tiku_backing_can_mutate(pool->backing)) {
        return NULL;
    }
    if (pool->free_head == NULL) {
        pool->fail++;                    /* exhausted, not misused */
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
 * @brief Return a block to the pool.
 *
 * Pushes it back onto the freelist head in O(1).  The pointer is checked
 * against the buffer and a block boundary first: with no MMU a stray free
 * silently corrupts the freelist and later allocations return wild pointers.
 *
 * @param pool   Pool the block belongs to
 * @param ptr    Pointer previously returned by tiku_pool_alloc
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if ptr is
 *         outside the pool or not aligned to a block boundary
 */
tiku_mem_err_t tiku_pool_free(tiku_pool_t *pool, void *ptr)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    uint8_t *block;
    tiku_mem_arch_size_t offset;

    if (!pool_valid(pool) || pool->reset_failed || pool->used_count == 0u || ptr == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }
    if (!tiku_backing_can_mutate(pool->backing)) return TIKU_MEM_ERR_BUSY;

    block = (uint8_t *)ptr;

    /* Validate: ptr must fall within the pool's buffer */
    if ((uintptr_t)block < (uintptr_t)pool->buf ||
        (uintptr_t)block - (uintptr_t)pool->buf >= pool->block_count * pool->block_size) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Validate: ptr must be aligned to a block boundary */
    offset = (tiku_mem_arch_size_t)(block - pool->buf);
    if (offset % pool->block_size != 0) {
        return TIKU_MEM_ERR_INVALID;
    }
    if (block_is_already_free(pool, block)) {
        return TIKU_MEM_ERR_INVALID;
    }

#if TIKU_POOL_DEBUG
    /*
     * Poison freed block to catch use-after-free during development.
     * The first sizeof(void *) bytes are used for the freelist pointer,
     * so poison only the remaining bytes. 0xDE is a recognizable
     * pattern in hex dumps ("dead"). Skipped for NVM-tier pools: a direct
     * CPU store bus-faults on program-op NVM (MRAM/Flash), and poisoning
     * through the region program op would erase+reprogram the block's sector
     * on every free.
     */
    if (!pool->nvm) {
        tiku_mem_arch_size_t ptr_bytes;
        tiku_mem_arch_size_t i;

        ptr_bytes = (tiku_mem_arch_size_t)sizeof(void *);
        for (i = ptr_bytes; i < pool->block_size; i++) {
            block[i] = 0xDE;
        }
    }
#endif

    /* Push onto freelist head (NVM-aware: program op on MRAM/Flash) */
    {
        tiku_mem_err_t err = pool_write_next(pool, block, pool->free_head);
        if (err != TIKU_MEM_OK) return err;
    }
    pool->free_head = block;

    pool->used_count--;

    return TIKU_MEM_OK;
}

/**
 * @brief Fill a stats struct with the pool's current state.
 *
 * Total and used bytes are the block size times the block and used counts, and
 * the peak is reported the same way.
 *
 * @param pool   Pool to query
 * @param stats  Output structure (caller-provided)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on a NULL argument
 */
tiku_mem_err_t tiku_pool_stats(const tiku_pool_t *pool,
                                tiku_mem_stats_t *stats)
{
    if (!pool_valid(pool) || stats == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    stats->total_bytes = pool->block_size * pool->block_count;
    stats->used_bytes  = pool->block_size * pool->used_count;
    stats->peak_bytes  = pool->block_size * pool->peak_count;
    stats->alloc_count = pool->used_count;
    stats->fail_count  = pool->fail;

    return TIKU_MEM_OK;
}

/**
 * @brief Reset the pool, returning all blocks to the freelist.
 *
 * Re-chains every block and zeroes used_count, keeping the peak so it stays a
 * lifetime figure.  Unlike an arena this is O(n), since each block needs its
 * next-pointer written -- cheap at the pool sizes in use.
 *
 * @param pool   Pool to reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if pool is NULL
 */
tiku_mem_err_t tiku_pool_reset(tiku_pool_t *pool)
{
    tiku_mem_err_t err;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!pool_valid(pool)) return TIKU_MEM_ERR_INVALID;
    if (!tiku_backing_can_mutate(pool->backing)) return TIKU_MEM_ERR_BUSY;
    err = build_freelist(pool);
    pool->reset_failed = (err != TIKU_MEM_OK);
    if (err == TIKU_MEM_OK) pool->used_count = 0;
    return err;
}
