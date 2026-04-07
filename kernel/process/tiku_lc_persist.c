/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lc_persist.c - NVM-backed local continuation persistence
 *
 * Allows protothreads to survive power loss by storing their
 * continuation state (line number) in non-volatile memory via
 * the kernel persist store.  On resume after a power cycle the
 * protothread picks up from its last LC_SET_PERSISTENT point
 * instead of restarting from the beginning.
 *
 * This is the enabling mechanism for intermittent computing on
 * battery-free devices: a multi-step protocol handshake can
 * checkpoint after each step, so energy spent on completed steps
 * is never wasted.
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

#include "tiku.h"
#include "tiku_lc.h"

#if TIKU_LC_PERSISTENT

#include "kernel/memory/tiku_mem.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Maximum number of persistent LC slots (one per persistent protothread) */
#ifndef TIKU_LC_PERSIST_MAX_SLOTS
#define TIKU_LC_PERSIST_MAX_SLOTS  8
#endif

/*---------------------------------------------------------------------------*/
/* NVM BACKING STORAGE                                                       */
/*---------------------------------------------------------------------------*/

#ifdef PLATFORM_MSP430
#define LC_NVM_PERSISTENT __attribute__((section(".persistent")))
#else
#define LC_NVM_PERSISTENT
#endif

/**
 * NVM pool: one lc_t-sized buffer per slot.
 *
 * On MSP430, placed in FRAM via the .persistent section.
 * Each tiku_lc_persist_register() call claims the next free slot
 * and hands its address to the persist store.
 */
static LC_NVM_PERSISTENT uint8_t
    lc_nvm_pool[TIKU_LC_PERSIST_MAX_SLOTS * sizeof(lc_t)] = {0};

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/*
 * The persist store struct is placed in FRAM (.persistent section) so
 * that entry metadata (key, value_len, write_count, magic, valid)
 * survives power cycles.  On reboot, tiku_persist_init() scans the
 * FRAM-backed entries for valid magic numbers and recovers them.
 *
 * Why this works:
 *   - The .persistent section is linker-placed at a fixed address.
 *   - First boot after flash: FRAM is zeroed by the linker, no valid
 *     magic numbers, all entries cleared — clean start.
 *   - After power cycle: FRAM retains the entries with valid magic.
 *     tiku_persist_init() finds them, count is restored.
 *   - tiku_persist_register() finds the existing key (from previous
 *     boot) and updates only the runtime fields (fram_ptr, capacity).
 *     The stored value_len and write_count are preserved.
 *   - The NVM data pool is also linker-placed, so fram_ptr addresses
 *     are stable across boots.
 *
 * MPU consideration:
 *   All functions that modify the store entries (init, register, write,
 *   delete) are wrapped with MPU unlock/lock because the store is now
 *   in FRAM.  Read-only functions (persist_read, persist_find) don't
 *   need MPU unlock.
 */
static LC_NVM_PERSISTENT tiku_persist_store_t lc_persist_store = {0};
static uint8_t           lc_persist_initialized;
static uint8_t           lc_nvm_next_slot;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the LC persistent store
 *
 * Scans the FRAM-backed store for magic-validated entries that
 * survived a power cycle.  Invalid entries are cleared.
 * Safe to call multiple times — subsequent calls are no-ops.
 */
void tiku_lc_persist_init(void)
{
    uint16_t mpu_state;
    tiku_mem_arch_size_t i;

    if (lc_persist_initialized) {
        return;
    }

    /* Do NOT memset the store — it's in FRAM and may contain valid
     * entries from a previous boot.  tiku_persist_init() handles
     * validation: entries with correct magic are kept, others are
     * cleared (requires MPU unlock since the store is in FRAM). */
    mpu_state = tiku_mpu_unlock_nvm();
    tiku_persist_init(&lc_persist_store);
    tiku_mpu_lock_nvm(mpu_state);

    /* Recover the next-free-slot index from any entries that survived
     * the power cycle.  Each persistent LC entry's fram_ptr is the
     * address of an lc_t-sized chunk in lc_nvm_pool — find the highest
     * occupied slot and resume allocation just past it.  Without this,
     * post-reboot allocations would re-hand out slots already owned by
     * recovered keys. */
    lc_nvm_next_slot = 0;
    for (i = 0; i < TIKU_PERSIST_MAX_ENTRIES; i++) {
        tiku_persist_entry_t *e = &lc_persist_store.entries[i];
        if (e->valid && e->magic == TIKU_PERSIST_MAGIC &&
            e->fram_ptr >= lc_nvm_pool &&
            e->fram_ptr <  lc_nvm_pool + sizeof(lc_nvm_pool)) {
            size_t offset = (size_t)(e->fram_ptr - lc_nvm_pool);
            uint8_t slot  = (uint8_t)(offset / sizeof(lc_t));
            if ((uint8_t)(slot + 1) > lc_nvm_next_slot) {
                lc_nvm_next_slot = (uint8_t)(slot + 1);
            }
        }
    }

    lc_persist_initialized = 1;
}

/**
 * @brief Register a persistent LC slot under a key
 *
 * Allocates the next sizeof(lc_t) chunk from the NVM pool and
 * registers it in the persist store.  If the key already exists
 * (duplicate registration in the same boot, or survived a reboot
 * and was recovered by tiku_persist_init), this is a no-op: the
 * existing slot and stored value are preserved without burning
 * a fresh slot from the pool.
 *
 * @param key  Null-terminated key (max 7 chars + NUL)
 * @return 0 on success, -1 if store not initialized,
 *         -2 if pool exhausted, -3 on persist error
 */
int tiku_lc_persist_register(const char *key)
{
    tiku_mem_err_t err;
    uint8_t *slot_buf;
    uint16_t mpu_state;
    uint8_t  probe;
    tiku_mem_arch_size_t probe_len;

    if (!lc_persist_initialized) {
        return -1;
    }

    /* If the key already has an entry — duplicate registration in
     * this boot, or an entry recovered from FRAM by tiku_persist_init
     * — reuse it instead of burning another pool slot.  The existing
     * fram_ptr already points into lc_nvm_pool (slot recovery in
     * tiku_lc_persist_init bumped lc_nvm_next_slot past it), so the
     * mapping stays consistent and write_count is preserved. */
    if (tiku_persist_read(&lc_persist_store, key,
                          &probe, sizeof(probe), &probe_len)
        != TIKU_MEM_ERR_NOT_FOUND) {
        return 0;
    }

    if (lc_nvm_next_slot >= TIKU_LC_PERSIST_MAX_SLOTS) {
        return -2;
    }

    slot_buf = &lc_nvm_pool[lc_nvm_next_slot * sizeof(lc_t)];

    /* Register writes to FRAM-backed entry metadata */
    mpu_state = tiku_mpu_unlock_nvm();
    err = tiku_persist_register(&lc_persist_store, key,
                                slot_buf, sizeof(lc_t));
    tiku_mpu_lock_nvm(mpu_state);

    if (err != TIKU_MEM_OK) {
        return -3;
    }

    lc_nvm_next_slot++;
    return 0;
}

/**
 * @brief Save an lc_t value to NVM
 *
 * Writes the continuation line number into the persist store.
 * Updates both the NVM data buffer and the FRAM-backed entry
 * metadata (value_len, write_count).
 *
 * Unlocks the MPU internally — the PT_*_PERSISTENT macros do not
 * unlock NVM, and the persist write touches both the data slot and
 * the FRAM-backed entry metadata (value_len, write_count).
 *
 * @param key  Key previously registered with tiku_lc_persist_register
 * @param val  The lc_t value (line number) to persist
 * @return 0 on success, negative on error
 */
int tiku_lc_persist_save(const char *key, lc_t val)
{
    uint16_t mpu_state;
    tiku_mem_err_t err;

    mpu_state = tiku_mpu_unlock_nvm();
    err = tiku_persist_write(&lc_persist_store, key,
                             (const uint8_t *)&val,
                             sizeof(lc_t));
    tiku_mpu_lock_nvm(mpu_state);

    return (int)err;
}

/**
 * @brief Load an lc_t value from NVM
 *
 * Reads the stored continuation line number.  Returns non-zero if
 * the key does not exist or has no stored value (first boot or
 * after LC_CLEAR_PERSISTENT), in which case *val is untouched and
 * the protothread starts from the beginning.
 *
 * Does not need MPU unlock (read-only).
 *
 * @param key  Key previously registered with tiku_lc_persist_register
 * @param val  Output: the stored lc_t value
 * @return 0 on success, negative if not found or empty
 */
int tiku_lc_persist_load(const char *key, lc_t *val)
{
    uint8_t buf[sizeof(lc_t)];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;

    err = tiku_persist_read(&lc_persist_store, key,
                            buf, sizeof(buf), &out_len);
    if (err != TIKU_MEM_OK || out_len != sizeof(lc_t)) {
        return -1;
    }

    memcpy(val, buf, sizeof(lc_t));

    /* A stored value of 0 means "start from beginning" — treat as empty */
    if (*val == 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief Clear the NVM entry for a key
 *
 * Removes the continuation state so the next boot starts fresh.
 * Unlocks MPU internally since the entry metadata is in FRAM.
 *
 * @param key  Key to clear
 * @return 0 on success, negative on error
 */
int tiku_lc_persist_clear(const char *key)
{
    uint16_t mpu_state;
    int rc;

    mpu_state = tiku_mpu_unlock_nvm();
    rc = (int)tiku_persist_delete(&lc_persist_store, key);
    tiku_mpu_lock_nvm(mpu_state);

    return rc;
}

/**
 * @brief Reset the NVM value to 0 without deleting the entry
 *
 * Writes 0 to the persist store so LC_RESUME_PERSISTENT treats
 * it as "start from beginning".  The key stays registered so
 * future saves succeed — unlike clear which removes the key.
 *
 * Unlocks the MPU internally — the PT_*_PERSISTENT macros do not
 * unlock NVM, and the persist write touches both the data slot and
 * the FRAM-backed entry metadata.
 *
 * @param key  Key to reset
 * @return 0 on success, negative on error
 */
int tiku_lc_persist_reset(const char *key)
{
    lc_t zero = 0;
    uint16_t mpu_state;
    tiku_mem_err_t err;

    mpu_state = tiku_mpu_unlock_nvm();
    err = tiku_persist_write(&lc_persist_store, key,
                             (const uint8_t *)&zero,
                             sizeof(lc_t));
    tiku_mpu_lock_nvm(mpu_state);

    return (int)err;
}

#endif /* TIKU_LC_PERSISTENT */
