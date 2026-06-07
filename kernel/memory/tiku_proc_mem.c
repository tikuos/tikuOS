/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_proc_mem.c - Per-process isolated memory contexts
 *
 * Binds an SRAM scratch arena, an NVM persistent arena, an optional
 * HIFRAM bulk arena, and a set of write-back cached regions to a
 * single process identifier. Isolation is enforced at allocation time:
 * tiku_proc_alloc() routes requests to the correct arena, which is
 * bounds-checked by the arena allocator. A request can never escape
 * the arenas owned by the process that issued it.
 *
 * This is cheap and correct for cooperative scheduling — processes do
 * not preempt each other, so there is no concurrent-access concern.
 * The context owns its arenas and caches; destroying it flushes all
 * dirty caches and resets every arena.
 *
 * The SRAM and NVM arenas are allocated up front by
 * tiku_proc_mem_create(); the HIFRAM arena is opt-in via
 * tiku_proc_mem_attach_hifram(), so processes that never need the
 * upper FRAM bank pay nothing for it. tiku_proc_mem_stats() exposes
 * each arena's live usage (total / used / peak / alloc count) for
 * per-process memory accounting, the same kind of figure the shell
 * `ps` command and the /proc nodes surface as sram_used / fram_used.
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
 * The context is zeroed first, so the HIFRAM arena (and every cache
 * slot) starts inactive; HIFRAM is added later via
 * tiku_proc_mem_attach_hifram(). If the NVM arena fails to allocate
 * after the SRAM arena succeeded, the SRAM arena is rolled back (reset
 * and marked inactive) so the caller is left with no half-built
 * context. Backing memory comes from the tier allocator, so a request
 * can fail when a tier's backing pool is exhausted.
 *
 * @param pmem       Context to initialize
 * @param pid        Owning process identifier (used as arena id)
 * @param tier       Tier hint (AUTO places each arena in its natural tier)
 * @param sram_size  SRAM arena capacity in bytes (0 to skip)
 * @param nvm_size   NVM arena capacity in bytes (0 to skip)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if pmem is NULL
 *         or both sizes are zero, or the tier-allocator error
 *         (e.g. TIKU_MEM_ERR_NOMEM) from the arena that could not be
 *         created
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
 * Flushes and destroys all attached cached regions, then resets every
 * arena the context owns (SRAM, NVM, and the HIFRAM arena if one was
 * attached), marks each inactive, and finally marks the context itself
 * inactive. After this call, all memory previously allocated through
 * this context is invalid.
 *
 * Caches are flushed before destruction so that any dirty data in
 * SRAM is persisted to FRAM. This ensures no silent data loss when
 * a process exits. Note that the arenas are reset, not securely wiped
 * — their bytes (including any in FRAM) remain until overwritten by a
 * later allocation; use a secure reset path explicitly if a process
 * held secrets. The reset rewinds each arena's bump pointer but does
 * not return its sub-buffer to the tier pool (the tier allocator is
 * bump-only), so the underlying tier capacity is not reclaimed here.
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
 * @brief Allocate within a process context (bounds-checked)
 *
 * Routes the allocation to the correct arena based on the requested
 * memory tier:
 *   - SRAM / NVM: straight to that arena.
 *   - HIFRAM: to the HIFRAM arena, but only if one has been attached
 *     via tiku_proc_mem_attach_hifram(); otherwise NULL (a deliberate
 *     hard signal rather than a silent fall-through to NVM, so a
 *     placement bug in code that genuinely needs HIFRAM surfaces).
 *   - AUTO: prefers HIFRAM for large requests (size at or above
 *     TIKU_TIER_AUTO_HIFRAM_THRESHOLD) when a HIFRAM arena is attached,
 *     then SRAM, then NVM, taking the first arena that has room. This
 *     mirrors the tier-allocator AUTO policy.
 *
 * Isolation guarantee: allocations cannot escape the process's arenas.
 * The arena allocator itself enforces bounds — each alloc is checked
 * against the arena's capacity, and these arenas are the only ones this
 * context can reach.
 *
 * Note: unlike resolve_tier() in the tier allocator, the AUTO branch
 * here omits the TIKU_TIER_AUTO_HIFRAM_THRESHOLD > 0 guard, so with the
 * threshold set to 0 any request would prefer HIFRAM when an arena is
 * attached (rather than disabling HIFRAM routing). In practice the
 * threshold is left at its default, and HIFRAM is reached only when an
 * arena has actually been attached, so the two policies agree.
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
 * @brief Attach a HIFRAM arena to an existing process context
 *
 * Lazy opt-in path for a process that needs a chunk of the upper FRAM
 * bank (HIFRAM on FR5994 / FR6989, reachable only under
 * MEMORY_MODEL=large). It carves a HIFRAM-tier arena via
 * tiku_tier_arena_create() and stores it in pmem->hifram_arena, after
 * which tiku_proc_alloc() with TIKU_MEM_HIFRAM (or AUTO for large
 * requests) routes here instead of returning NULL.
 *
 * Kept separate from tiku_proc_mem_create() so processes that never
 * touch HIFRAM neither carry the attach cost nor depend on a tier that
 * doesn't exist on small parts. Re-attaching is rejected rather than
 * silently re-allocating: overwriting pmem->hifram_arena would abandon
 * the previous HIFRAM sub-buffer in the tier pool with no way to
 * reclaim it (the tier allocator is bump-only).
 *
 * On parts without HIFRAM, or under MEMORY_MODEL=small, the HIFRAM tier
 * is uninitialized, so the underlying tiku_tier_arena_create() returns
 * TIKU_MEM_ERR_NOMEM and that code propagates out cleanly.
 *
 * @param pmem  Active process memory context
 * @param size  HIFRAM arena capacity in bytes (must be > 0)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad arguments
 *         or if a HIFRAM arena is already attached, TIKU_MEM_ERR_NOMEM
 *         if the HIFRAM tier is unavailable or lacks room
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
 * Delegates to tiku_arena_stats() for the requested tier's arena,
 * filling total / used / peak / alloc-count for that one arena. This is
 * the per-process memory-accounting hook: it answers "how much of this
 * process's SRAM (or NVM, or HIFRAM) scratch is in use right now."
 *
 * AUTO is not a queryable tier (there is no AUTO arena) and is
 * rejected. A HIFRAM query on a context that never attached a HIFRAM
 * arena returns TIKU_MEM_ERR_NOT_FOUND rather than zeroed stats, so the
 * caller can tell "0 bytes used" from "no such arena."
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
