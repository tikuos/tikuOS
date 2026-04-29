/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_hibernate.c - Hibernate/resume orchestration for memory subsystem
 *
 * Coordinates an orderly transition to and from deep sleep by flushing
 * all write-back caches, persisting a hibernate marker (boot count +
 * timestamp), and reloading cached regions on warm resume.
 *
 * Why a hibernate layer:
 *   Entering low-power mode (LPMx.5 on MSP430) requires every dirty
 *   SRAM-cached region to be flushed to FRAM beforehand — otherwise
 *   volatile data is lost. On wake-up the system needs to distinguish
 *   a cold boot (first power-on, no valid FRAM state) from a warm
 *   resume (returning from hibernate with valid FRAM data). The
 *   hibernate marker in the persist store, protected by the standard
 *   magic-number scheme, provides that discrimination. A monotonic
 *   boot count lets application code detect how many sleep/wake cycles
 *   have occurred (useful for wear budgeting and diagnostics).
 *
 * Interaction with other subsystems:
 *   - tiku_cache_flush_all() — persists every dirty cached region
 *   - tiku_persist — stores/reads the hibernate marker
 *   - tiku_mpu — MPU is unlocked during marker writes and cache flushes
 *   - tiku_mem_init() — must be called before tiku_mem_resume()
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
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/*
 * The hibernate marker is a small struct persisted to FRAM via the
 * persist store. It contains a magic number for validation, a
 * monotonic boot count, and a timestamp supplied by the caller.
 *
 * The persist store and its FRAM backing buffer are module-private.
 * The store is initialized lazily on the first call to hibernate or
 * resume.
 */

static tiku_persist_store_t hibernate_store;
static uint8_t              hibernate_initialized;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the hibernate persist store if not already done
 *
 * Initializes the store and registers the marker key. Safe to call
 * multiple times — subsequent calls are no-ops.
 *
 * @param fram_buf   FRAM buffer for the marker (caller-provided)
 * @return TIKU_MEM_OK on success, or an error code
 */
static tiku_mem_err_t hibernate_ensure_init(uint8_t *fram_buf)
{
    tiku_mem_err_t err;

    if (hibernate_initialized) {
        return TIKU_MEM_OK;
    }

    memset(&hibernate_store, 0, sizeof(hibernate_store));
    err = tiku_persist_init(&hibernate_store);
    if (err != TIKU_MEM_OK) {
        return err;
    }

    err = tiku_persist_register(&hibernate_store,
                                 TIKU_HIBERNATE_KEY,
                                 fram_buf,
                                 sizeof(tiku_hibernate_marker_t));
    if (err != TIKU_MEM_OK) {
        return err;
    }

    hibernate_initialized = 1;
    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Reset the hibernate subsystem to uninitialized state
 *
 * Clears the internal persist store and initialization flag so the
 * next hibernate or resume call re-registers the marker key with
 * value_len = 0.  This is required between independent test groups
 * that each expect boot_count to start from 1, because
 * hibernate_initialized lives in SRAM and survives across calls
 * (unlike a real power cycle where SRAM is lost).
 *
 * Not needed in production — a real LPMx.5 wake clears SRAM,
 * which naturally resets hibernate_initialized to 0.
 *
 * @note Only for test use.  Has no effect on NVM contents beyond
 *       clearing the SRAM-resident persist-store cache.
 */
void tiku_mem_hibernate_reset(void)
{
    memset(&hibernate_store, 0, sizeof(hibernate_store));
    hibernate_initialized = 0;
}

/**
 * @brief Prepare the memory subsystem for hibernation
 *
 * Flushes all dirty write-back caches to FRAM, then writes a hibernate
 * marker containing the current boot count (incremented) and caller-
 * supplied timestamp. The entire operation runs in a single MPU-unlocked
 * critical section to minimize overhead.
 *
 * Call this immediately before entering LPMx.5 or any deep sleep mode
 * that loses SRAM contents.
 *
 * @param fram_buf   FRAM buffer for the hibernate marker (must reside
 *                   in NVM, at least sizeof(tiku_hibernate_marker_t))
 * @param timestamp  Caller-supplied timestamp (RTC ticks, epoch, etc.)
 * @return TIKU_MEM_OK on success, or an error code
 */
tiku_mem_err_t tiku_mem_hibernate(uint8_t *fram_buf, uint32_t timestamp)
{
    tiku_hibernate_marker_t marker;
    tiku_hibernate_marker_t existing;
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    uint16_t mpu_state;

    if (fram_buf == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    err = hibernate_ensure_init(fram_buf);
    if (err != TIKU_MEM_OK) {
        return err;
    }

    /* Read existing marker to get current boot count (may fail on
     * first hibernate — that's fine, we start from zero). */
    memset(&existing, 0, sizeof(existing));
    if (tiku_persist_read(&hibernate_store, TIKU_HIBERNATE_KEY,
                           (uint8_t *)&existing, sizeof(existing),
                           &out_len) == TIKU_MEM_OK &&
        existing.magic == TIKU_HIBERNATE_MAGIC) {
        marker.boot_count = existing.boot_count + 1;
    } else {
        marker.boot_count = 1;
    }

    marker.magic     = TIKU_HIBERNATE_MAGIC;
    marker.timestamp = timestamp;

    /* Single MPU-unlocked section for cache flush + marker write */
    mpu_state = tiku_mpu_unlock_nvm();

    /* Flush all dirty caches to FRAM */
    tiku_cache_flush_all();

    /* Write the hibernate marker */
    err = tiku_persist_write(&hibernate_store, TIKU_HIBERNATE_KEY,
                              (const uint8_t *)&marker, sizeof(marker));

    tiku_mpu_lock_nvm(mpu_state);

    return err;
}

/**
 * @brief Check for a warm resume after hibernation
 *
 * Call after tiku_mem_init() on every boot. Reads the persist store
 * for a valid hibernate marker. If found, reloads all registered
 * cached regions from FRAM (resync SRAM with FRAM) and returns
 * TIKU_MEM_OK for a warm resume. If no valid marker is found,
 * returns TIKU_MEM_ERR_NOT_FOUND indicating a cold boot.
 *
 * On warm resume the marker is preserved so the boot count remains
 * readable via tiku_mem_hibernate_get_marker().
 *
 * @param fram_buf    FRAM buffer that was used for the hibernate marker
 * @param marker_out  Output: hibernate marker (may be NULL if not needed)
 * @return TIKU_MEM_OK if warm resume (valid marker found),
 *         TIKU_MEM_ERR_NOT_FOUND if cold boot (no marker),
 *         or another error code on failure
 */
tiku_mem_err_t tiku_mem_resume(uint8_t *fram_buf,
                                tiku_hibernate_marker_t *marker_out)
{
    tiku_hibernate_marker_t marker;
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    tiku_mem_arch_size_t i;

    if (fram_buf == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    err = hibernate_ensure_init(fram_buf);
    if (err != TIKU_MEM_OK) {
        return err;
    }

    /* Try to read the hibernate marker */
    memset(&marker, 0, sizeof(marker));
    err = tiku_persist_read(&hibernate_store, TIKU_HIBERNATE_KEY,
                             (uint8_t *)&marker, sizeof(marker),
                             &out_len);

    if (err != TIKU_MEM_OK) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    /* Validate the marker */
    if (marker.magic != TIKU_HIBERNATE_MAGIC) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    /* Valid warm resume — reload all cached regions from FRAM */
    for (i = 0; i < tiku_cache_get_count(); i++) {
        tiku_cached_region_t *r = tiku_cache_get_region(i);

        if (r != NULL && r->active) {
            tiku_cache_reload(r);
        }
    }

    if (marker_out != NULL) {
        *marker_out = marker;
    }

    return TIKU_MEM_OK;
}
