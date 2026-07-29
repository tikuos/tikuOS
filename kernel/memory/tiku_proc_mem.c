/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_proc_mem.c - per-process isolated memory contexts.
 *
 * Binds an SRAM scratch arena, an NVM persistent arena, an optional HIFRAM bulk
 * arena and a set of cached regions to one process id.  tiku_proc_alloc() routes
 * to the right arena, so an allocation cannot escape the process that made it.
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
 * @brief Create an isolated memory context for a process.
 *
 * Carves an SRAM and/or NVM arena from the tier allocator, either size zero to
 * skip that tier; AUTO puts each in its natural tier.  If the second arena
 * fails the first is rolled back, so a caller never sees a half-built context.
 *
 * @param pmem       Context to initialize
 * @param pid        Owning process identifier (used as arena id)
 * @param tier       Tier hint (AUTO places each arena in its natural tier)
 * @param sram_size  SRAM arena capacity in bytes (0 to skip)
 * @param nvm_size   NVM arena capacity in bytes (0 to skip)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if pmem is NULL
 *         or both sizes are zero, or the tier-allocator error from the
 *         arena that could not be created
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
 * @brief Destroy a process memory context.
 *
 * Flushes and destroys every attached cache first, so a dirty page is persisted
 * rather than silently lost, then resets each arena.  Reset is not a secure
 * wipe, and the tier allocator being bump-only means capacity is not reclaimed.
 *
 * @param pmem  Context to destroy
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if pmem is NULL
 *         or already inactive
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

    if (pmem->hifram_arena.active) {
        tiku_arena_reset(&pmem->hifram_arena);
        pmem->hifram_arena.active = 0;
    }

    pmem->active = 0;

    return TIKU_MEM_OK;
}

/**
 * @brief Allocate within a process context (bounds-checked).
 *
 * SRAM and NVM go straight to their arena; HIFRAM returns NULL unless one was
 * attached, deliberately, so a placement bug surfaces rather than falling
 * through.  AUTO prefers HIFRAM for large requests, then SRAM, then NVM.
 *
 * @param pmem  Active process memory context
 * @param tier  Memory tier (SRAM, NVM, HIFRAM, or AUTO)
 * @param size  Bytes requested (must be > 0)
 * @return Pointer to the allocated memory, or NULL on failure (NULL
 *         context, inactive context, zero size, unknown tier, missing
 *         HIFRAM arena, or no arena with room)
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

    case TIKU_MEM_HIFRAM:
        /* Caller must have attached a HIFRAM arena first via
         * tiku_proc_mem_attach_hifram(). NULL on missing/inactive
         * is the cleanest signal — the alternative (silently
         * routing to NVM) would mask placement bugs in user code
         * that legitimately needs HIFRAM (e.g., crossing the
         * 64 KB barrier for large lookup tables). */
        if (pmem->hifram_arena.active) {
            return tiku_arena_alloc(&pmem->hifram_arena, size);
        }
        return NULL;

    case TIKU_MEM_AUTO:
        /* Prefer HIFRAM for large requests if attached, then SRAM,
         * then NVM. Mirrors the tier-allocator AUTO policy. */
        if (size >= TIKU_TIER_AUTO_HIFRAM_THRESHOLD &&
            pmem->hifram_arena.active) {
            ptr = tiku_arena_alloc(&pmem->hifram_arena, size);
            if (ptr != NULL) {
                return ptr;
            }
        }
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
 * @brief Attach a HIFRAM arena to an existing process context.
 *
 * A lazy opt-in kept out of create(), so a process that never touches HIFRAM
 * neither pays for it nor depends on a tier small parts lack.  Re-attaching is
 * rejected: overwriting the arena would strand its sub-buffer unreclaimably.
 *
 * @param pmem  Active process memory context
 * @param size  HIFRAM arena capacity in bytes
 * @return TIKU_MEM_OK on success
 */
tiku_mem_err_t tiku_proc_mem_attach_hifram(tiku_proc_mem_t *pmem,
                                            tiku_mem_arch_size_t size)
{
    if (pmem == NULL || !pmem->active || size == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Already attached? Reject rather than silently re-allocate —
     * the caller almost certainly didn't mean to abandon their
     * existing HIFRAM arena. */
    if (pmem->hifram_arena.active) {
        return TIKU_MEM_ERR_INVALID;
    }

    return tiku_tier_arena_create(&pmem->hifram_arena,
                                   TIKU_MEM_HIFRAM, size, pmem->pid);
}

/**
 * @brief Attach a cached region to a process context.
 *
 * Records ownership of an already-created region -- it does not create one --
 * so destroying the context flushes and destroys every attached cache.
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
 * @brief Get statistics for a process arena.
 *
 * Fills total, used, peak and allocation count for one tier's arena -- the
 * per-process accounting hook.  AUTO is not queryable, and a HIFRAM query with
 * no arena attached returns NOT_FOUND, so "0 used" differs from "no arena".
 *
 * @param pmem   Active process memory context
 * @param tier   Which arena to query (SRAM, NVM, or HIFRAM; not AUTO)
 * @param stats  Output statistics (must be non-NULL)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad arguments
 *         or AUTO/unknown tier, TIKU_MEM_ERR_NOT_FOUND if a HIFRAM arena
 *         was requested but none is attached
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
    case TIKU_MEM_HIFRAM:
        if (!pmem->hifram_arena.active) {
            return TIKU_MEM_ERR_NOT_FOUND;
        }
        return tiku_arena_stats(&pmem->hifram_arena, stats);
    default:
        return TIKU_MEM_ERR_INVALID;
    }
}
