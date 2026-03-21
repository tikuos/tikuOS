/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_proc_mem.c - Per-process isolated memory contexts
 *
 * Binds an SRAM scratch arena, an NVM persistent arena, and a set of
 * write-back cached regions to a single process identifier. Isolation
 * is enforced at allocation time: tiku_proc_alloc() routes requests
 * to the correct arena, which is bounds-checked by the arena allocator.
 *
 * This is cheap and correct for cooperative scheduling — processes do
 * not preempt each other, so there is no concurrent-access concern.
 * The context owns its arenas and caches; destroying it flushes all
 * dirty caches and resets both arenas.
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
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Create an isolated memory context for a process
 *
 * Allocates an SRAM arena and/or an NVM arena from the tier allocator,
 * both bound to the given process identifier. Either size may be zero
 * to skip that tier — a process that only needs scratch SRAM can pass
 * nvm_size = 0 and vice versa.
 *
 * The tier parameter controls arena placement:
 *   - TIKU_MEM_SRAM / TIKU_MEM_NVM: both arenas forced to that tier
 *     (only makes sense if one size is zero, otherwise the NVM arena
 *     on SRAM would defeat persistence).
 *   - TIKU_MEM_AUTO: SRAM arena placed in SRAM, NVM arena placed in
 *     NVM — the natural default.
 *
 * @param pmem       Context to initialize
 * @param pid        Owning process identifier (used as arena id)
 * @param tier       Tier hint (AUTO places each arena in its natural tier)
 * @param sram_size  SRAM arena capacity in bytes (0 to skip)
 * @param nvm_size   NVM arena capacity in bytes (0 to skip)
 * @return TIKU_MEM_OK on success
 */
tiku_mem_err_t tiku_proc_mem_create(tiku_proc_mem_t *pmem,
                                     uint8_t pid,
                                     tiku_mem_tier_t tier,
                                     tiku_mem_arch_size_t sram_size,
                                     tiku_mem_arch_size_t nvm_size)
{
    tiku_mem_err_t err;
    tiku_mem_tier_t sram_tier;
    tiku_mem_tier_t nvm_tier;

    if (pmem == NULL || (sram_size == 0 && nvm_size == 0)) {
        return TIKU_MEM_ERR_INVALID;
    }

    memset(pmem, 0, sizeof(*pmem));
    pmem->pid = pid;

    /*
     * Resolve tier for each arena. AUTO places each arena in its
     * natural tier. An explicit tier forces both arenas there.
     */
    if (tier == TIKU_MEM_AUTO) {
        sram_tier = TIKU_MEM_SRAM;
        nvm_tier  = TIKU_MEM_NVM;
    } else {
        sram_tier = tier;
        nvm_tier  = tier;
    }

    /* Create SRAM arena if requested */
    if (sram_size > 0) {
        err = tiku_tier_arena_create(&pmem->sram_arena, sram_tier,
                                      sram_size, pid);
        if (err != TIKU_MEM_OK) {
            return err;
        }
    }

    /* Create NVM arena if requested */
    if (nvm_size > 0) {
        err = tiku_tier_arena_create(&pmem->nvm_arena, nvm_tier,
                                      nvm_size, pid);
        if (err != TIKU_MEM_OK) {
            /* Roll back the SRAM arena if it was created */
            if (sram_size > 0) {
                tiku_arena_reset(&pmem->sram_arena);
                pmem->sram_arena.active = 0;
            }
            return err;
        }
    }

    pmem->active = 1;

    return TIKU_MEM_OK;
}

/**
 * @brief Destroy a process memory context
 *
 * Flushes and destroys all attached cached regions, then resets both
 * arenas and marks the context inactive. After this call, all memory
 * previously allocated through this context is invalid.
 *
 * Caches are flushed before destruction so that any dirty data in
 * SRAM is persisted to FRAM. This ensures no silent data loss when
 * a process exits.
 *
 * @param pmem  Context to destroy
 * @return TIKU_MEM_OK on success
 */
tiku_mem_err_t tiku_proc_mem_destroy(tiku_proc_mem_t *pmem)
{
    uint8_t i;

    if (pmem == NULL || !pmem->active) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Flush and destroy all attached cached regions */
    for (i = 0; i < pmem->cache_count; i++) {
        if (pmem->caches[i] != NULL && pmem->caches[i]->active) {
            tiku_cache_flush(pmem->caches[i]);
            tiku_cache_destroy(pmem->caches[i]);
        }
        pmem->caches[i] = NULL;
    }
    pmem->cache_count = 0;

    /* Reset arenas (reclaim all allocations) */
    if (pmem->sram_arena.active) {
        tiku_arena_reset(&pmem->sram_arena);
        pmem->sram_arena.active = 0;
    }

    if (pmem->nvm_arena.active) {
        tiku_arena_reset(&pmem->nvm_arena);
        pmem->nvm_arena.active = 0;
    }

    pmem->active = 0;

    return TIKU_MEM_OK;
}

/**
 * @brief Allocate within a process context (bounds-checked)
 *
 * Routes the allocation to the correct arena based on the requested
 * memory tier. AUTO prefers SRAM and falls back to NVM if the SRAM
 * arena is full or was not created.
 *
 * Isolation guarantee: allocations cannot escape the process's arenas.
 * The arena allocator itself enforces bounds — each alloc is checked
 * against the arena's capacity.
 *
 * @param pmem  Active process memory context
 * @param tier  Memory tier (SRAM, NVM, or AUTO)
 * @param size  Bytes requested (must be > 0)
 * @return Pointer to the allocated memory, or NULL on failure
 */
void *tiku_proc_alloc(tiku_proc_mem_t *pmem,
                       tiku_mem_tier_t tier,
                       tiku_mem_arch_size_t size)
{
    void *ptr;

    if (pmem == NULL || !pmem->active || size == 0) {
        return NULL;
    }

    switch (tier) {
    case TIKU_MEM_SRAM:
        return tiku_arena_alloc(&pmem->sram_arena, size);

    case TIKU_MEM_NVM:
        return tiku_arena_alloc(&pmem->nvm_arena, size);

    case TIKU_MEM_AUTO:
        /* Prefer SRAM; fall back to NVM */
        if (pmem->sram_arena.active) {
            ptr = tiku_arena_alloc(&pmem->sram_arena, size);
            if (ptr != NULL) {
                return ptr;
            }
        }
        if (pmem->nvm_arena.active) {
            return tiku_arena_alloc(&pmem->nvm_arena, size);
        }
        return NULL;

    default:
        return NULL;
    }
}

/**
 * @brief Attach a cached region to a process context
 *
 * Adds an already-created cached region to the process context's
 * ownership list. When the context is destroyed, all attached caches
 * are flushed and destroyed automatically.
 *
 * The cached region must already be created via tiku_cache_create().
 * This function only records ownership — it does not create the cache.
 *
 * @param pmem    Active process memory context
 * @param region  Cached region to attach (must be active)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_FULL if at capacity
 */
tiku_mem_err_t tiku_proc_mem_attach_cache(tiku_proc_mem_t *pmem,
                                           tiku_cached_region_t *region)
{
    if (pmem == NULL || !pmem->active ||
        region == NULL || !region->active) {
        return TIKU_MEM_ERR_INVALID;
    }

    if (pmem->cache_count >= TIKU_PROC_MEM_MAX_CACHES) {
        return TIKU_MEM_ERR_FULL;
    }

    pmem->caches[pmem->cache_count] = region;
    pmem->cache_count++;

    return TIKU_MEM_OK;
}

/**
 * @brief Get statistics for a process arena
 *
 * Delegates to tiku_arena_stats() for the requested tier's arena.
 *
 * @param pmem   Active process memory context
 * @param tier   Which arena to query (SRAM or NVM, not AUTO)
 * @param stats  Output statistics
 * @return TIKU_MEM_OK on success
 */
tiku_mem_err_t tiku_proc_mem_stats(const tiku_proc_mem_t *pmem,
                                    tiku_mem_tier_t tier,
                                    tiku_mem_stats_t *stats)
{
    if (pmem == NULL || !pmem->active || stats == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    switch (tier) {
    case TIKU_MEM_SRAM:
        return tiku_arena_stats(&pmem->sram_arena, stats);
    case TIKU_MEM_NVM:
        return tiku_arena_stats(&pmem->nvm_arena, stats);
    default:
        return TIKU_MEM_ERR_INVALID;
    }
}
