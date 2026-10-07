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
 * timestamp), and reloads cached regions on warm resume.  The marker
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
 * The hibernate marker (magic, monotonic boot count, the caller's timestamp
 * and a CRC) is kept in the caller's NVM buffer through a module-private
 * persist store, initialized on the first call to hibernate or resume.  The
 * store's control block, which holds the marker's length, is in SRAM.
 */

static tiku_persist_store_t hibernate_store;
static uint8_t              hibernate_initialized;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the hibernate persist store if not already done.
 *
 * Zeroes and initializes the store and registers the marker key; later calls
 * in the same boot return TIKU_MEM_OK at once.
 *
 * @param fram_buf   NVM buffer for the marker (caller-provided)
 * @return TIKU_MEM_OK on success, or an error code
 */
static tiku_mem_err_t hibernate_ensure_init(uint8_t *fram_buf)
{
    tiku_mem_err_t err;

    if (hibernate_initialized &&
        hibernate_store.entries[0].fram_ptr == fram_buf) {
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

    /* The fixed-size marker carries its own magic and payload CRC. */
    hibernate_store.entries[0].value_len = sizeof(tiku_hibernate_marker_t);
    hibernate_initialized = 1;
    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Reset the hibernate subsystem to uninitialised state.
 *
 * Clears the SRAM registration to model an SRAM-losing restart.
 * A valid NVM marker still supplies the previous boot count.
 *
 * @note Test use only.  Clears the SRAM persist store; NVM is untouched.
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
 * count and the caller's timestamp.  A failed cache flush skips the marker.
 *
 * @param fram_buf   NVM buffer for the hibernate marker, at least
 *                   sizeof(tiku_hibernate_marker_t)
 * @param timestamp  Caller-supplied timestamp (RTC ticks, epoch, etc.)
 * @return TIKU_MEM_OK on success, or an error code
 * @note Call immediately before entering a sleep mode that loses SRAM.
 */
tiku_mem_err_t tiku_mem_hibernate(uint8_t *fram_buf, uint32_t timestamp)
{
    tiku_hibernate_marker_t marker;
    tiku_hibernate_marker_t existing;
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;

    if (fram_buf == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    err = hibernate_ensure_init(fram_buf);
    if (err != TIKU_MEM_OK) {
        return err;
    }

    /* A missing or invalid marker (the first hibernate) starts at 1. */
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

    /* Each call opens its own NVM window; a failed flush is not retried. */
    err = tiku_cache_flush_all();

    if (err == TIKU_MEM_OK) {
        err = tiku_persist_write(&hibernate_store, TIKU_HIBERNATE_KEY,
                                  (const uint8_t *)&marker, sizeof(marker));
    }

    return err;
}

/**
 * @brief Check for a warm resume after hibernation.
 *
 * A valid marker means warm resume: every cached region is reloaded from NVM
 * and the marker is preserved so the boot count stays readable.  No marker
 * means a cold boot.
 *
 * @param fram_buf    NVM buffer that was used for the hibernate marker
 * @param marker_out  Output: hibernate marker (may be NULL if not needed)
 * @return TIKU_MEM_OK if warm resume (valid marker found),
 *         TIKU_MEM_ERR_NOT_FOUND if cold boot (no marker),
 *         or another error code on failure
 * @note Call after tiku_mem_init() on every boot.
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

    memset(&marker, 0, sizeof(marker));
    err = tiku_persist_read(&hibernate_store, TIKU_HIBERNATE_KEY,
                             (uint8_t *)&marker, sizeof(marker),
                             &out_len);

    if (err != TIKU_MEM_OK) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    /* Magic and payload CRC: the CRC rejects a torn marker whose magic
     * survived. */
    if (marker.magic != TIKU_HIBERNATE_MAGIC ||
        marker.crc != tiku_nvm_crc32(&marker.boot_count,
                                     2 * sizeof(uint32_t))) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    /* Valid warm resume: reload all cached regions from NVM. */
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
