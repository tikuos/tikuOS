/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_hibernate.c - hibernate/resume orchestration for the memory subsystem.
 *
 * Flushes every write-back cache, persists a hibernate marker (boot count and
 * timestamp), and reloads cached regions on warm resume.  The marker is what
 * distinguishes a cold boot from a return out of deep sleep.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"
#include "tiku_nvm_mirror.h"
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
 * @brief Reset the hibernate subsystem to uninitialised state.
 *
 * Test-only.  hibernate_initialized lives in SRAM and survives across calls,
 * unlike a real power cycle, so independent test groups that each expect
 * boot_count to start at 1 need this between them.
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
 * @brief Prepare the memory subsystem for hibernation.
 *
 * Flushes every dirty cache, then writes a marker holding the incremented boot
 * count and the caller's timestamp, all in one MPU window.  Call immediately
 * before entering a sleep mode that loses SRAM.
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
        existing.magic == TIKU_HIBERNATE_MAGIC &&
        existing.crc == tiku_nvm_crc32(&existing.boot_count,
                                       2 * sizeof(uint32_t))) {
        marker.boot_count = existing.boot_count + 1;
    } else {
        marker.boot_count = 1;
    }

    marker.magic     = TIKU_HIBERNATE_MAGIC;
    marker.timestamp = timestamp;
    marker.crc       = tiku_nvm_crc32(&marker.boot_count,
                                      2 * sizeof(uint32_t));

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
 * @brief Check for a warm resume after hibernation.
 *
 * Call after tiku_mem_init() on every boot.  A valid marker means warm resume:
 * every cached region is reloaded from NVM and the marker is preserved so the
 * boot count stays readable.  No marker means a cold boot.
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

    /* Validate the marker: magic AND payload CRC.  Magic-only let a
     * torn marker write masquerade as a valid warm-resume record. */
    if (marker.magic != TIKU_HIBERNATE_MAGIC ||
        marker.crc != tiku_nvm_crc32(&marker.boot_count,
                                     2 * sizeof(uint32_t))) {
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
