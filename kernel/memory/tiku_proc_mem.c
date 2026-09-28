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
#include "tiku_mem_internal.h"
#include "tiku_reclaim_internal.h"
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
static tiku_mem_err_t proc_mem_create(tiku_proc_mem_t *pmem,
                                     uint8_t pid,
                                     tiku_mem_tier_t tier,
                                     tiku_mem_arch_size_t sram_size,
                                     tiku_mem_arch_size_t nvm_size,
                                     const tiku_mem_request_t *owned)
{
    tiku_mem_err_t err;
    tiku_mem_tier_t sram_tier;
    tiku_mem_tier_t nvm_tier;

    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);

    if (pmem == NULL || (sram_size == 0 && nvm_size == 0)) {
        return TIKU_MEM_ERR_INVALID;
    }
    if (tiku_backing_output_busy(&pmem->sram_arena) ||
        tiku_backing_output_busy(&pmem->nvm_arena) ||
        tiku_backing_output_busy(&pmem->hifram_arena)) {
        return TIKU_MEM_ERR_BUSY;
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
        err = tiku_tier_arena_create_opts(&pmem->sram_arena, sram_tier,
                                           sram_size, pid, owned);
        if (err != TIKU_MEM_OK) {
            return err;
        }
    }

    /* Create NVM arena if requested */
    if (nvm_size > 0) {
        tiku_mem_request_t options = TIKU_MEM_REQUEST_DEFAULT;
        if (owned) { options = *owned; options.owner_slot++; }
        err = tiku_tier_arena_create_opts(&pmem->nvm_arena, nvm_tier,
                                           nvm_size, pid, owned ? &options : NULL);
        if (err != TIKU_MEM_OK) {
            /* Roll back the SRAM arena if it was created */
            if (sram_size > 0) {
#if TIKU_MEM_RECLAIM_ENABLE
                /* A held restore owns partial claims until stopped cleanup;
                 * public destroy must not bypass the coordinator's fence. */
                if (owned && !tiku_mem_owner_available(owned->owner)) return err;
#endif
                tiku_mem_err_t rollback = tiku_arena_destroy(&pmem->sram_arena);
                if (rollback != TIKU_MEM_OK) return rollback;
            }
            return err;
        }
    }

    pmem->active = 1;
#if TIKU_MEM_RECLAIM_ENABLE
    if (owned) { pmem->owner = owned->owner; pmem->owner_key_base = owned->owner_slot; }
#endif

    return TIKU_MEM_OK;
}

tiku_mem_err_t tiku_proc_mem_create(tiku_proc_mem_t *pmem, uint8_t pid,
    tiku_mem_tier_t tier, tiku_mem_arch_size_t sram_size, tiku_mem_arch_size_t nvm_size)
{ return proc_mem_create(pmem, pid, tier, sram_size, nvm_size, NULL); }

#if TIKU_MEM_RECLAIM_ENABLE
tiku_mem_err_t tiku_proc_mem_set_owner(tiku_proc_mem_t *pmem, tiku_mem_owner_t owner,
                                       uint16_t key_base)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!pmem || !pmem->active || pmem->cache_count || !key_base || key_base > UINT16_MAX - 2u ||
        pmem->owner.slot_plus_one || !tiku_reclaim_process_owner_valid(owner, pmem, sizeof *pmem))
        return TIKU_MEM_ERR_INVALID;
    return tiku_reclaim_context_tag(pmem, owner, key_base);
}

tiku_mem_err_t tiku_proc_mem_create_owned(tiku_proc_mem_t *pmem, uint8_t pid,
    tiku_mem_tier_t tier, tiku_mem_arch_size_t sram_size, tiku_mem_arch_size_t nvm_size,
    tiku_mem_owner_t owner, uint16_t key_base)
{
    tiku_mem_request_t options = TIKU_MEM_REQUEST_DEFAULT;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!key_base || key_base > UINT16_MAX - 2u ||
        !tiku_reclaim_process_owner_valid(owner, pmem, sizeof *pmem)) return TIKU_MEM_ERR_INVALID;
    options.owner = owner; options.owner_slot = key_base;
    return proc_mem_create(pmem, pid, tier, sram_size, nvm_size, &options);
}
#endif

/**
 * @brief Destroy a process memory context.
 *
 * Flushes and destroys every attached cache first, so a dirty page is persisted
 * rather than silently lost, then releases every owned arena's backing.
 * The caller must first stop all users of these objects. This is not a wipe.
 *
 * @param pmem  Context to destroy
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if pmem is NULL
 *         or already inactive
 */
tiku_mem_err_t tiku_proc_mem_destroy(tiku_proc_mem_t *pmem)
{
    uint8_t i;
    tiku_arena_t *arenas[3];
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);

    if (pmem == NULL || !pmem->active) {
        return TIKU_MEM_ERR_INVALID;
    }
#if TIKU_MEM_RECLAIM_ENABLE
    if (pmem->owner.slot_plus_one && !tiku_mem_owner_available(pmem->owner))
        return TIKU_MEM_ERR_BUSY;
#endif

    /* Flush and destroy all attached cached regions */
    for (i = 0; i < pmem->cache_count; i++) {
        if (pmem->caches[i] != NULL && pmem->caches[i]->active) {
            tiku_mem_err_t status = tiku_cache_flush(pmem->caches[i]);
            if (status != TIKU_MEM_OK) {
                return status;
            }
            status = tiku_cache_destroy(pmem->caches[i]);
            if (status != TIKU_MEM_OK) return status;
        }
        pmem->caches[i] = NULL;
    }
    pmem->cache_count = 0;

    arenas[0] = &pmem->sram_arena;
    arenas[1] = &pmem->nvm_arena;
    arenas[2] = &pmem->hifram_arena;
    for (i = 0; i < 3; i++) {
        if (arenas[i]->active) {
            tiku_mem_err_t status = tiku_arena_reset(arenas[i]);
            if (status == TIKU_MEM_OK) status = tiku_arena_destroy(arenas[i]);
            if (status != TIKU_MEM_OK) return status;
        }
    }

    pmem->active = 0;

    return TIKU_MEM_OK;
}

/**
 * @brief Allocate within a process context (bounds-checked).
 *
 * SRAM and NVM go straight to their arena; HIFRAM returns NULL unless one was
 * attached, deliberately, so a placement bug surfaces rather than falling
 * through. AUTO uses only arenas backed by SRAM or HIFRAM.
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
        /* Use local capacity and inspect backing tiers, not field names. */
#if TIKU_TIER_AUTO_HIFRAM_THRESHOLD > 0
        if (size >= TIKU_TIER_AUTO_HIFRAM_THRESHOLD &&
            pmem->hifram_arena.active &&
            pmem->hifram_arena.tier == TIKU_MEM_HIFRAM) {
            ptr = tiku_arena_alloc(&pmem->hifram_arena, size);
            if (ptr != NULL) {
                return ptr;
            }
        }
#endif
        if (pmem->sram_arena.active &&
            (pmem->sram_arena.tier == TIKU_MEM_SRAM ||
             pmem->sram_arena.tier == TIKU_MEM_HIFRAM)) {
            ptr = tiku_arena_alloc(&pmem->sram_arena, size);
            if (ptr != NULL) {
                return ptr;
            }
        }
        if (pmem->nvm_arena.active &&
            (pmem->nvm_arena.tier == TIKU_MEM_SRAM ||
             pmem->nvm_arena.tier == TIKU_MEM_HIFRAM)) {
            ptr = tiku_arena_alloc(&pmem->nvm_arena, size);
            if (ptr != NULL) {
                return ptr;
            }
        }
        if (pmem->hifram_arena.active &&
            pmem->hifram_arena.tier == TIKU_MEM_HIFRAM) {
            return tiku_arena_alloc(&pmem->hifram_arena, size);
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
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (pmem == NULL || !pmem->active || size == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Already attached? Reject rather than silently re-allocate —
     * the caller almost certainly didn't mean to abandon their
     * existing HIFRAM arena. */
    if (pmem->hifram_arena.active) {
        return TIKU_MEM_ERR_INVALID;
    }

#if TIKU_MEM_RECLAIM_ENABLE
    if (pmem->owner.slot_plus_one) {
        tiku_mem_request_t options = TIKU_MEM_REQUEST_DEFAULT;
        options.owner = pmem->owner; options.owner_slot = (uint16_t)(pmem->owner_key_base + 2u);
        return tiku_tier_arena_create_opts(&pmem->hifram_arena, TIKU_MEM_HIFRAM,
                                           size, pmem->pid, &options);
    }
#endif
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
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (pmem == NULL || !pmem->active ||
        region == NULL || !region->active) {
        return TIKU_MEM_ERR_INVALID;
    }
#if TIKU_MEM_RECLAIM_ENABLE
    if (pmem->owner.slot_plus_one) return TIKU_MEM_ERR_INVALID;
#endif

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
