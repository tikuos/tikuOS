/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cache.c - Write-back cache for hot FRAM regions
 *
 * Implements SRAM write-back caching of FRAM-backed data. Frequently
 * updated structures (network state, sensor buffers, counters) are
 * kept in fast SRAM during active processing and flushed to persistent
 * FRAM only at explicit sync points or before entering low-power mode.
 *
 * Why write-back caching:
 *   FRAM writes on MSP430 consume roughly 3x more energy per access
 *   than SRAM writes and contribute to cell wear (~10^15 cycle
 *   endurance). By absorbing rapid read-modify-write bursts in SRAM,
 *   the cache reduces both energy consumption and wear. The trade-off
 *   is that data in SRAM is volatile — a power failure before flush
 *   loses uncommitted changes. Callers control this trade-off by
 *   choosing when to flush.
 *
 * How MPU interaction works:
 *   FRAM is write-protected by default via the MPU. Individual flushes
 *   unlock/relock around each write. The batch flush
 *   (tiku_cache_flush_all) unlocks once for the entire batch to
 *   minimize the unlock/lock overhead — important because each
 *   MPU register write goes through the password-protected MPUCTL0.
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
/* GLOBAL REGION TABLE                                                       */
/*---------------------------------------------------------------------------*/

/*
 * A flat table of pointers to all registered cached regions. This
 * allows tiku_cache_flush_all() to iterate every active region
 * without requiring the caller to maintain a list.
 *
 * Why a pointer table and not embedded list nodes:
 *   The cached_region_t structs live wherever the caller places them
 *   (typically static storage). An intrusive linked list would add
 *   a next-pointer to the struct and complicate destruction (must
 *   unlink). A flat pointer table is simpler, bounded, and trivial
 *   to scan on a small MCU.
 */
static tiku_cached_region_t *cache_table[TIKU_CACHE_MAX_REGIONS];
static tiku_mem_arch_size_t  cache_count;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Register a region in the global table
 *
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_FULL if table is full
 */
static tiku_mem_err_t table_add(tiku_cached_region_t *region)
{
    if (cache_count >= TIKU_CACHE_MAX_REGIONS) {
        return TIKU_MEM_ERR_FULL;
    }

    cache_table[cache_count] = region;
    cache_count++;

    return TIKU_MEM_OK;
}

/**
 * @brief Remove a region from the global table
 *
 * Swaps the last entry into the vacated slot to keep the table
 * compact. O(n) scan but n <= TIKU_CACHE_MAX_REGIONS (small).
 *
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_NOT_FOUND if not in table
 */
static tiku_mem_err_t table_remove(tiku_cached_region_t *region)
{
    tiku_mem_arch_size_t i;

    for (i = 0; i < cache_count; i++) {
        if (cache_table[i] == region) {
            /* Swap with last entry and shrink */
            cache_count--;
            cache_table[i] = cache_table[cache_count];
            cache_table[cache_count] = NULL;
            return TIKU_MEM_OK;
        }
    }

    return TIKU_MEM_ERR_NOT_FOUND;
}

/*---------------------------------------------------------------------------*/
/* CACHE FUNCTIONS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Create a cached region over a FRAM address
 *
 * Copies current FRAM contents into SRAM so the working copy starts
 * in sync with persistent storage.
 */
tiku_mem_err_t tiku_cache_create(tiku_cached_region_t *region,
                                  uint8_t *fram_addr,
                                  uint8_t *sram_buf,
                                  tiku_mem_arch_size_t size)
{
    tiku_mem_err_t err;

    if (region == NULL || fram_addr == NULL ||
        sram_buf == NULL || size == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Register in global table first — fail early if full */
    err = table_add(region);
    if (err != TIKU_MEM_OK) {
        return err;
    }

    region->sram_cache   = sram_buf;
    region->fram_backing = fram_addr;
    region->size         = size;
    region->dirty        = 0;
    region->active       = 1;

    /* Seed SRAM cache with current FRAM contents */
    tiku_mem_arch_nvm_read(sram_buf, fram_addr, size);

    return TIKU_MEM_OK;
}

/**
 * @brief Get a pointer to the SRAM working copy
 *
 * Marks the region dirty since the caller will presumably write
 * through the returned pointer. This is the fast path — all
 * subsequent reads and writes hit SRAM until an explicit flush.
 */
void *tiku_cache_get(tiku_cached_region_t *region)
{
    if (region == NULL || !region->active) {
        return NULL;
    }

    region->dirty = 1;

    return region->sram_cache;
}

/**
 * @brief Mark a cached region as dirty
 */
tiku_mem_err_t tiku_cache_mark_dirty(tiku_cached_region_t *region)
{
    if (region == NULL || !region->active) {
        return TIKU_MEM_ERR_INVALID;
    }

    region->dirty = 1;

    return TIKU_MEM_OK;
}

/**
 * @brief Flush a single cached region from SRAM back to FRAM
 *
 * Unlocks the MPU, copies SRAM to FRAM via the HAL, and relocks.
 * No-op if the region is clean.
 */
tiku_mem_err_t tiku_cache_flush(tiku_cached_region_t *region)
{
    uint16_t saved;

    if (region == NULL || !region->active) {
        return TIKU_MEM_ERR_INVALID;
    }

    if (!region->dirty) {
        return TIKU_MEM_OK;
    }

    saved = tiku_mpu_unlock_nvm();
    tiku_mem_arch_nvm_write(region->fram_backing,
                             region->sram_cache,
                             region->size);
    tiku_mpu_lock_nvm(saved);

    region->dirty = 0;

    return TIKU_MEM_OK;
}

/**
 * @brief Flush all registered cached regions
 *
 * Unlocks the MPU once for the entire batch. This is more efficient
 * than flushing individually when multiple regions are dirty, because
 * each MPU unlock/lock pair writes to the password-protected MPUCTL0
 * register.
 */
tiku_mem_err_t tiku_cache_flush_all(void)
{
    tiku_mem_arch_size_t i;
    uint16_t saved;
    uint8_t any_dirty = 0;

    /* Quick scan: skip MPU unlock entirely if nothing is dirty */
    for (i = 0; i < cache_count; i++) {
        if (cache_table[i]->active && cache_table[i]->dirty) {
            any_dirty = 1;
            break;
        }
    }

    if (!any_dirty) {
        return TIKU_MEM_OK;
    }

    /* Single MPU unlock for the entire batch */
    saved = tiku_mpu_unlock_nvm();

    for (i = 0; i < cache_count; i++) {
        tiku_cached_region_t *r = cache_table[i];

        if (r->active && r->dirty) {
            tiku_mem_arch_nvm_write(r->fram_backing,
                                     r->sram_cache,
                                     r->size);
            r->dirty = 0;
        }
    }

    tiku_mpu_lock_nvm(saved);

    return TIKU_MEM_OK;
}

/**
 * @brief Reload a cached region from FRAM into SRAM
 *
 * Overwrites the SRAM working copy with fresh FRAM contents. Useful
 * when external code (DMA, ISR, another subsystem) has written
 * directly to FRAM and the cache needs to resync.
 */
tiku_mem_err_t tiku_cache_reload(tiku_cached_region_t *region)
{
    if (region == NULL || !region->active) {
        return TIKU_MEM_ERR_INVALID;
    }

    tiku_mem_arch_nvm_read(region->sram_cache,
                            region->fram_backing,
                            region->size);

    region->dirty = 0;

    return TIKU_MEM_OK;
}

/**
 * @brief Get the number of registered cached regions
 *
 * Returns the current size of the global cache table. Used by the
 * hibernate layer to iterate all regions for reload on warm resume.
 */
tiku_mem_arch_size_t tiku_cache_get_count(void)
{
    return cache_count;
}

/**
 * @brief Get a cached region by index
 *
 * Returns a pointer to the region at the given table index, or NULL
 * if the index is out of range. Used by the hibernate layer.
 */
tiku_cached_region_t *tiku_cache_get_region(tiku_mem_arch_size_t index)
{
    if (index >= cache_count) {
        return NULL;
    }

    return cache_table[index];
}

/**
 * @brief Destroy a cached region
 *
 * Removes the region from the global table and clears the descriptor.
 * Does not flush — caller must flush before destroying if persistence
 * is needed.
 */
tiku_mem_err_t tiku_cache_destroy(tiku_cached_region_t *region)
{
    if (region == NULL || !region->active) {
        return TIKU_MEM_ERR_INVALID;
    }

    table_remove(region);

    region->sram_cache   = NULL;
    region->fram_backing = NULL;
    region->size         = 0;
    region->dirty        = 0;
    region->active       = 0;

    return TIKU_MEM_OK;
}
