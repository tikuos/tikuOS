/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_persist.c - Persistent FRAM key-value store implementation
 *
 * Implements a registry that maps short string keys to FRAM-backed
 * buffers. Entries are registered at boot with caller-provided FRAM
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

/*
 * Why init scans for magic numbers:
 *   On first boot, FRAM contains arbitrary values. After a reboot,
 *   FRAM retains whatever was written. The magic number lets init
 *   distinguish real entries (written by persist_register) from
 *   FRAM garbage — only entries with the correct magic and valid
 *   flag are counted as live.
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

/*
 * Why register preserves existing data:
 *   After a firmware update the application re-registers the same keys.
 *   If the key already exists (magic + valid + matching name), we update
 *   the FRAM pointer (it may have moved in the new binary) but keep the
 *   stored value_len, write_count, and the FRAM data intact. This lets
 *   configuration and calibration values survive across firmware updates.
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

/*
 * Why read copies to SRAM:
 *   FRAM on MSP430 has wait states (1-2 at higher clock speeds).
 *   Copying into an SRAM buffer means subsequent accesses to the
 *   data are at full CPU speed. The copy is also safer — the
 *   caller's buffer won't be affected by concurrent NVM writes.
 *
 *   Delegates to tiku_mem_arch_nvm_read() so the arch layer can
 *   handle platform-specific NVM access (memory-mapped FRAM on
 *   MSP430, SPI Flash elsewhere, etc.).
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

/*
 * Why write doesn't unlock MPU internally:
 *   On MSP430, FRAM writes require the MPU to be unlocked. Rather
 *   than unlocking/locking per write (expensive and risky if
 *   interrupted), the caller batches multiple writes in a single
 *   MPU-unlocked critical section and calls persist_write for each.
 *
 *   Delegates to tiku_mem_arch_nvm_write() so the arch layer can
 *   handle platform-specific NVM write sequences (direct write on
 *   memory-mapped FRAM, erase+program on Flash, etc.).
 *
 * Why wear tracking matters:
 *   FRAM has finite write endurance (~10^15 cycles typical for TI
 *   MSP430). While far higher than Flash, hot keys (e.g. a counter
 *   incremented every second) can approach limits over years.
 *   Tracking write_count lets the application detect and warn before
 *   a cell degrades.
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
