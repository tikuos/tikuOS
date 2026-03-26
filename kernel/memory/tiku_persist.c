/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_persist.c - Persistent NVM key-value store implementation
 *
 * Implements a registry that maps short string keys to NVM-backed
 * buffers. Entries are registered at boot with caller-provided NVM
 * regions. A magic number validates entries across reboots, and
 * write counts track wear for endurance monitoring.
 *
 * All NVM access is routed through the HAL (tiku_mem_arch_nvm_read/write)
 * so the kernel code stays platform-independent.
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
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Find an entry by key (linear scan)
 *
 * Scans valid entries for a matching key. Linear scan is appropriate
 * because the store is small (TIKU_PERSIST_MAX_ENTRIES <= 16 typical).
 *
 * @param store   Store to search
 * @param key     Null-terminated key to find
 * @return Pointer to the matching entry, or NULL if not found
 */
static tiku_persist_entry_t *persist_find(tiku_persist_store_t *store,
                                           const char *key)
{
    tiku_mem_arch_size_t i;

    for (i = 0; i < TIKU_PERSIST_MAX_ENTRIES; i++) {
        if (store->entries[i].valid &&
            store->entries[i].magic == TIKU_PERSIST_MAGIC &&
            strncmp(store->entries[i].key, key,
                    TIKU_PERSIST_MAX_KEY_LEN) == 0) {
            return &store->entries[i];
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the persistent store, recovering valid entries
 *
 * Scans all slots: entries with correct magic and valid flag are kept,
 * all others are cleared. Call once at boot.
 *
 * Why init scans for magic numbers:
 *   On first boot, NVM contains arbitrary values. After a reboot,
 *   NVM retains whatever was written. The magic number lets init
 *   distinguish real entries (written by persist_register) from
 *   NVM garbage — only entries with the correct magic and valid
 *   flag are counted as live.
 *
 * @param store   Store to initialize
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if store is NULL
 */
tiku_mem_err_t tiku_persist_init(tiku_persist_store_t *store)
{
    tiku_mem_arch_size_t i;
    tiku_mem_arch_size_t count;

    if (store == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    count = 0;
    for (i = 0; i < TIKU_PERSIST_MAX_ENTRIES; i++) {
        if (store->entries[i].magic == TIKU_PERSIST_MAGIC &&
            store->entries[i].valid) {
            count++;
        } else {
            memset(&store->entries[i], 0, sizeof(tiku_persist_entry_t));
        }
    }
    store->count = count;

    return TIKU_MEM_OK;
}

/**
 * @brief Register an NVM buffer under a key
 *
 * If the key already exists, updates the NVM pointer but preserves
 * existing data (survives firmware updates). Otherwise allocates the
 * first empty slot.
 *
 * Why register preserves existing data:
 *   After a firmware update the application re-registers the same keys.
 *   If the key already exists (magic + valid + matching name), we update
 *   the NVM pointer (it may have moved in the new binary) but keep the
 *   stored value_len, write_count, and the NVM data intact. This lets
 *   configuration and calibration values survive across firmware updates.
 *
 * @param store     Store to register into
 * @param key       Null-terminated key string
 * @param fram_buf  Pointer to caller-provided NVM buffer
 * @param capacity  Size of the NVM buffer in bytes
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID, or TIKU_MEM_ERR_FULL
 */
tiku_mem_err_t tiku_persist_register(tiku_persist_store_t *store,
                                     const char *key,
                                     uint8_t *fram_buf,
                                     tiku_mem_arch_size_t capacity)
{
    tiku_persist_entry_t *entry;
    tiku_mem_arch_size_t i;

    if (store == NULL || key == NULL || fram_buf == NULL || capacity == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Verify the FRAM buffer resides in NVM */
    if (!tiku_region_contains(fram_buf, capacity, TIKU_MEM_REGION_NVM)) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* If key already exists, update pointer but preserve data */
    entry = persist_find(store, key);
    if (entry != NULL) {
        entry->fram_ptr = fram_buf;
        entry->capacity = capacity;
        return TIKU_MEM_OK;
    }

    /* Find first empty slot */
    for (i = 0; i < TIKU_PERSIST_MAX_ENTRIES; i++) {
        if (!store->entries[i].valid) {
            entry = &store->entries[i];

            memset(entry, 0, sizeof(tiku_persist_entry_t));
            strncpy(entry->key, key, TIKU_PERSIST_MAX_KEY_LEN - 1);
            entry->key[TIKU_PERSIST_MAX_KEY_LEN - 1] = '\0';
            entry->fram_ptr    = fram_buf;
            entry->capacity    = capacity;
            entry->value_len   = 0;
            entry->write_count = 0;
            entry->magic       = TIKU_PERSIST_MAGIC;
            entry->valid       = 1;

            store->count++;
            return TIKU_MEM_OK;
        }
    }

    return TIKU_MEM_ERR_FULL;
}

/**
 * @brief Read a value from the persistent store into an SRAM buffer
 *
 * Copies from NVM to the caller's buffer via the HAL
 * (tiku_mem_arch_nvm_read). NVM may have wait states on some
 * platforms, so reading into SRAM gives faster subsequent access.
 * The copy is also safer — the caller's buffer won't be affected
 * by concurrent NVM writes.
 *
 * @param store     Store to read from
 * @param key       Key to look up
 * @param buf       Destination SRAM buffer
 * @param buf_size  Size of destination buffer
 * @param out_len   Output: actual length of stored value
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, TIKU_MEM_ERR_NOMEM,
 *         or TIKU_MEM_ERR_INVALID
 */
tiku_mem_err_t tiku_persist_read(tiku_persist_store_t *store,
                                  const char *key,
                                  uint8_t *buf,
                                  tiku_mem_arch_size_t buf_size,
                                  tiku_mem_arch_size_t *out_len)
{
    tiku_persist_entry_t *entry;

    if (store == NULL || key == NULL || buf == NULL || out_len == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    entry = persist_find(store, key);
    if (entry == NULL) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    /* Report required size even on failure so caller can retry */
    *out_len = entry->value_len;

    if (buf_size < entry->value_len) {
        return TIKU_MEM_ERR_NOMEM;
    }

    tiku_mem_arch_nvm_read(buf, entry->fram_ptr, entry->value_len);
    return TIKU_MEM_OK;
}

/**
 * @brief Write a value from SRAM into the persistent NVM store
 *
 * Copies data into the NVM buffer via the HAL
 * (tiku_mem_arch_nvm_write), updates value_len, and increments
 * write_count for wear monitoring.
 *
 * Why write doesn't unlock MPU internally:
 *   On platforms with NVM write-protection (e.g., MPU-guarded FRAM),
 *   writes require the protection to be unlocked. Rather than
 *   unlocking/locking per write (expensive and risky if interrupted),
 *   the caller batches multiple writes in a single unlocked critical
 *   section and calls persist_write for each.
 *
 * Why wear tracking matters:
 *   NVM technologies have finite write endurance. Hot keys (e.g. a
 *   counter incremented every second) can approach limits over years.
 *   Tracking write_count lets the application detect and warn before
 *   a cell degrades.
 *
 * @param store     Store to write into
 * @param key       Key to look up
 * @param data      Source data in SRAM
 * @param data_len  Length of source data
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, TIKU_MEM_ERR_NOMEM,
 *         or TIKU_MEM_ERR_INVALID
 */
tiku_mem_err_t tiku_persist_write(tiku_persist_store_t *store,
                                   const char *key,
                                   const uint8_t *data,
                                   tiku_mem_arch_size_t data_len)
{
    tiku_persist_entry_t *entry;

    if (store == NULL || key == NULL || data == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    entry = persist_find(store, key);
    if (entry == NULL) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    if (data_len > entry->capacity) {
        return TIKU_MEM_ERR_NOMEM;
    }

    tiku_mem_arch_nvm_write(entry->fram_ptr, data, data_len);
    entry->value_len = data_len;
    entry->write_count++;

    return TIKU_MEM_OK;
}

/**
 * @brief Delete an entry from the persistent store
 *
 * Clears the entry slot with memset so the key can no longer be found.
 *
 * @param store   Store to delete from
 * @param key     Key to delete
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, or TIKU_MEM_ERR_INVALID
 */
tiku_mem_err_t tiku_persist_delete(tiku_persist_store_t *store,
                                    const char *key)
{
    tiku_persist_entry_t *entry;

    if (store == NULL || key == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    entry = persist_find(store, key);
    if (entry == NULL) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    memset(entry, 0, sizeof(tiku_persist_entry_t));
    store->count--;

    return TIKU_MEM_OK;
}

/**
 * @brief Check wear level for a key
 *
 * Returns the write count and whether it exceeds the warning threshold.
 * NVM technologies have finite write endurance; tracking matters for
 * safety-critical systems and hot keys.
 *
 * @param store       Store to query
 * @param key         Key to check
 * @param write_count Output: number of writes to this key (may be NULL)
 * @return 1 if write_count exceeds threshold, 0 if within limits,
 *         or a negative tiku_mem_err_t on error
 */
int tiku_persist_wear_check(tiku_persist_store_t *store,
                             const char *key,
                             uint32_t *write_count)
{
    tiku_persist_entry_t *entry;

    if (store == NULL || key == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    entry = persist_find(store, key);
    if (entry == NULL) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    if (write_count != NULL) {
        *write_count = entry->write_count;
    }

    return (entry->write_count >= TIKU_PERSIST_WEAR_THRESHOLD) ? 1 : 0;
}
