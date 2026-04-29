/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tier.c - Tier-aware memory allocator
 *
 * Provides placement-aware allocation by managing pre-allocated
 * backing pools for SRAM and NVM. The caller specifies a memory
 * tier (SRAM, NVM, or AUTO) and the tier allocator carves the
 * buffer from the correct physical memory type, then initializes
 * a standard arena or pool over it.
 *
 * Why tier-aware allocation:
 *   On MCUs with both SRAM and NVM (FRAM), the caller often knows
 *   whether data is hot (frequent reads/writes, volatile OK) or
 *   cold (rarely updated, persistence desired). The tier allocator
 *   makes this intent explicit without requiring the caller to
 *   manage raw buffers and memory regions manually.
 *
 * Why AUTO:
 *   AUTO prefers SRAM (fast, low-energy) and falls back to NVM
 *   when SRAM is exhausted. This gives a simple "best effort"
 *   placement without the caller needing to know the memory budget.
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
 */
static tiku_mem_arch_size_t align_up(tiku_mem_arch_size_t size)
{
    const tiku_mem_arch_size_t mask = TIKU_MEM_ARCH_ALIGNMENT - 1U;
    return (size + mask) & ~mask;
}

/*---------------------------------------------------------------------------*/
/* BACKING POOLS                                                             */
/*---------------------------------------------------------------------------*/

/*
 * Static arrays that serve as the backing store for each memory tier.
 * On MSP430, the NVM pool is placed in FRAM via the .persistent
 * section. On host, both pools reside in regular BSS.
 *
 * The caller controls pool sizes via TIKU_TIER_SRAM_SIZE and
 * TIKU_TIER_NVM_SIZE defines (set before including tiku_mem.h).
 */

static uint8_t __attribute__((aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_sram_buf[TIKU_TIER_SRAM_SIZE];

#ifdef PLATFORM_MSP430
static uint8_t __attribute__((section(".persistent"),
                              aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_nvm_buf[TIKU_TIER_NVM_SIZE] = {0};
#else
static uint8_t __attribute__((aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_nvm_buf[TIKU_TIER_NVM_SIZE];
#endif

/*---------------------------------------------------------------------------*/
/* INTERNAL STATE                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Per-tier bump allocator state
 *
 * Each tier has a simple bump-pointer allocator over its backing pool.
 * Sub-buffers carved from here become the backing storage for user
 * arenas and pools.
 */
typedef struct {
    uint8_t              *buf;         /**< Backing pool start */
    tiku_mem_arch_size_t  capacity;    /**< Total pool size in bytes */
    tiku_mem_arch_size_t  offset;      /**< Current bump position */
    tiku_mem_arch_size_t  peak;        /**< Lifetime high-water mark */
    tiku_mem_arch_size_t  alloc_count; /**< Number of sub-allocations */
    uint8_t               initialized; /**< Non-zero after tiku_tier_init */
} tier_pool_state_t;

/* Indexed by tiku_mem_tier_t: [0] = SRAM, [1] = NVM */
static tier_pool_state_t tier_state[2];

/*---------------------------------------------------------------------------*/
/* TIER INIT                                                                 */
/*---------------------------------------------------------------------------*/

tiku_mem_err_t tiku_tier_init(void)
{
    tier_state[TIKU_MEM_SRAM].buf         = tier_sram_buf;
    tier_state[TIKU_MEM_SRAM].capacity    = TIKU_TIER_SRAM_SIZE;
    tier_state[TIKU_MEM_SRAM].offset      = 0;
    tier_state[TIKU_MEM_SRAM].peak        = 0;
    tier_state[TIKU_MEM_SRAM].alloc_count = 0;
    tier_state[TIKU_MEM_SRAM].initialized = 1;

    tier_state[TIKU_MEM_NVM].buf         = tier_nvm_buf;
    tier_state[TIKU_MEM_NVM].capacity    = TIKU_TIER_NVM_SIZE;
    tier_state[TIKU_MEM_NVM].offset      = 0;
    tier_state[TIKU_MEM_NVM].peak        = 0;
    tier_state[TIKU_MEM_NVM].alloc_count = 0;
    tier_state[TIKU_MEM_NVM].initialized = 1;

    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* RESOLVE AUTO                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Resolve TIKU_MEM_AUTO to a concrete tier
 *
 * Prefers SRAM if the requested size fits. Falls back to NVM.
 * SRAM and NVM tiers pass through unchanged.
 */
static tiku_mem_tier_t resolve_tier(tiku_mem_tier_t tier,
                                     tiku_mem_arch_size_t size)
{
    tiku_mem_arch_size_t aligned;

    if (tier != TIKU_MEM_AUTO) {
        return tier;
    }

    /* Prefer SRAM if it has room; fall back to NVM */
    aligned = align_up(size);
    if (tier_state[TIKU_MEM_SRAM].initialized &&
        aligned <= tier_state[TIKU_MEM_SRAM].capacity -
                   tier_state[TIKU_MEM_SRAM].offset) {
        return TIKU_MEM_SRAM;
    }

    return TIKU_MEM_NVM;
}

/*---------------------------------------------------------------------------*/
/* BUMP ALLOCATE FROM TIER POOL                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Carve a sub-buffer from a tier's backing pool
 *
 * Simple bump-pointer allocation. Returns an aligned pointer or
 * NULL if the tier is not initialized or lacks capacity.
 *
 * @param tier  Concrete tier (must not be AUTO)
 * @param size  Bytes needed (will be aligned up)
 * @return Pointer to the sub-buffer, or NULL
 */
static uint8_t *tier_bump_alloc(tiku_mem_tier_t tier,
                                 tiku_mem_arch_size_t size)
{
    tier_pool_state_t *ts = &tier_state[tier];
    tiku_mem_arch_size_t aligned = align_up(size);
    uint8_t *ptr;

    if (!ts->initialized) {
        return NULL;
    }

    if (aligned > ts->capacity - ts->offset) {
        return NULL;
    }

    ptr = ts->buf + ts->offset;
    ts->offset += aligned;
    ts->alloc_count++;

    if (ts->offset > ts->peak) {
        ts->peak = ts->offset;
    }

    return ptr;
}

/*---------------------------------------------------------------------------*/
/* TIER ARENA CREATE                                                         */
/*---------------------------------------------------------------------------*/

tiku_mem_err_t tiku_tier_arena_create(tiku_arena_t *arena,
                                       tiku_mem_tier_t tier,
                                       tiku_mem_arch_size_t size,
                                       uint8_t id)
{
    tiku_mem_tier_t resolved;
    tiku_mem_arch_size_t aligned;
    uint8_t *buf;

    if (arena == NULL || size == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    resolved = resolve_tier(tier, size);
    buf = tier_bump_alloc(resolved, size);

    if (buf == NULL) {
        return TIKU_MEM_ERR_NOMEM;
    }

    /*
     * Initialize the arena struct directly rather than calling
     * tiku_arena_create(). Reasons:
     *
     * 1. The backing pool's memory is managed by the tier allocator,
     *    not by individual arenas. Calling tiku_arena_create() would
     *    invoke tiku_region_claim() on a sub-range that overlaps the
     *    tier pool's own claim, which the region registry rejects.
     *
     * 2. The bump pointer from tier_bump_alloc() is already aligned,
     *    so no base-alignment adjustment is needed.
     */
    aligned = align_up(size);

    arena->buf      = buf;
    arena->capacity = aligned;
    arena->offset   = 0;
    arena->peak     = 0;
    arena->count    = 0;
    arena->id       = id;
    arena->active   = 1;
    arena->tier     = resolved;

    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* TIER POOL CREATE                                                          */
/*---------------------------------------------------------------------------*/

tiku_mem_err_t tiku_tier_pool_create(tiku_pool_t *pool,
                                      tiku_mem_tier_t tier,
                                      tiku_mem_arch_size_t block_size,
                                      tiku_mem_arch_size_t block_count,
                                      uint8_t id)
{
    tiku_mem_tier_t resolved;
    tiku_mem_arch_size_t aligned_blk;
    tiku_mem_arch_size_t min_blk;
    tiku_mem_arch_size_t total;
    uint8_t *buf;
    tiku_mem_err_t err;

    if (pool == NULL || block_size == 0 || block_count == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    /*
     * Compute total buffer size needed. This must mirror the
     * alignment logic in tiku_pool_create() so we allocate
     * exactly enough space.
     */
    aligned_blk = align_up(block_size);
    min_blk = align_up((tiku_mem_arch_size_t)sizeof(void *));
    if (aligned_blk < min_blk) {
        aligned_blk = min_blk;
    }
    total = aligned_blk * block_count;

    resolved = resolve_tier(tier, total);
    buf = tier_bump_alloc(resolved, total);

    if (buf == NULL) {
        return TIKU_MEM_ERR_NOMEM;
    }

    /*
     * For NVM-backed pools, freelist construction writes next-pointers
     * into FRAM blocks. The MPU must be temporarily unlocked. On host
     * the MPU functions are no-ops, so this is safe everywhere.
     */
    if (resolved == TIKU_MEM_NVM) {
        uint16_t saved = tiku_mpu_unlock_nvm();
        err = tiku_pool_create(pool, buf, block_size, block_count, id);
        tiku_mpu_lock_nvm(saved);
    } else {
        err = tiku_pool_create(pool, buf, block_size, block_count, id);
    }

    if (err == TIKU_MEM_OK) {
        pool->tier = resolved;
    }

    return err;
}

/*---------------------------------------------------------------------------*/
/* TIER QUERY                                                                */
/*---------------------------------------------------------------------------*/

tiku_mem_err_t tiku_tier_get(const uint8_t *ptr,
                              tiku_mem_tier_t *out_tier)
{
    int i;
    uintptr_t addr;

    if (ptr == NULL || out_tier == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    addr = (uintptr_t)ptr;

    /* Check the tier allocator's own backing pools first.
     * This works on both host and target — the backing pools may
     * not be in the platform's region table on host. */
    for (i = 0; i < 2; i++) {
        if (tier_state[i].initialized) {
            uintptr_t pool_start = (uintptr_t)tier_state[i].buf;

            if (addr >= pool_start &&
                (addr - pool_start) <
                    (uintptr_t)tier_state[i].capacity) {
                *out_tier = (tiku_mem_tier_t)i;
                return TIKU_MEM_OK;
            }
        }
    }

    /* Fall back to region registry for non-tier-managed memory */
    {
        tiku_mem_region_type_t region_type;
        tiku_mem_err_t err;

        err = tiku_region_get_type(ptr, &region_type);
        if (err == TIKU_MEM_OK) {
            switch (region_type) {
            case TIKU_MEM_REGION_SRAM:
                *out_tier = TIKU_MEM_SRAM;
                return TIKU_MEM_OK;
            case TIKU_MEM_REGION_NVM:
                *out_tier = TIKU_MEM_NVM;
                return TIKU_MEM_OK;
            default:
                break;
            }
        }
    }

    return TIKU_MEM_ERR_NOT_FOUND;
}

/*---------------------------------------------------------------------------*/
/* TIER STATS                                                                */
/*---------------------------------------------------------------------------*/

tiku_mem_err_t tiku_tier_stats(tiku_mem_tier_t tier,
                                tiku_mem_stats_t *stats)
{
    const tier_pool_state_t *ts;

    if (stats == NULL || tier == TIKU_MEM_AUTO) {
        return TIKU_MEM_ERR_INVALID;
    }

    if (tier != TIKU_MEM_SRAM && tier != TIKU_MEM_NVM) {
        return TIKU_MEM_ERR_INVALID;
    }

    ts = &tier_state[tier];
    if (!ts->initialized) {
        return TIKU_MEM_ERR_INVALID;
    }

    stats->total_bytes = ts->capacity;
    stats->used_bytes  = ts->offset;
    stats->peak_bytes  = ts->peak;
    stats->alloc_count = ts->alloc_count;

    return TIKU_MEM_OK;
}
