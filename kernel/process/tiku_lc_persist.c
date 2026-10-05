/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lc_persist.c - NVM-backed local continuation persistence.
 *
 * Stores a protothread's continuation state in the kernel persist store, so
 * after a power cycle it resumes from its last LC_SET_PERSISTENT point.
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

/* The persist store and NVM pool must live in a region the kernel recognizes
 * as NVM, or tiku_persist_register() rejects the buffer at registration time.
 * TIKU_DURABLE is that placement on every target.  Only the host build opts
 * out: its region table has no NVM class for an arbitrary section. */
#if defined(TIKU_TEST_HOST)
#define LC_NVM_PERSISTENT
#else
#define LC_NVM_PERSISTENT TIKU_DURABLE
#endif

/*
 * NVM pool: one lc_t-sized buffer per slot, placed in the durable section.
 * Each register call claims the next free slot and hands its address to the
 * persist store.
 */
static LC_NVM_PERSISTENT uint8_t
    lc_nvm_pool[TIKU_LC_PERSIST_MAX_SLOTS * sizeof(lc_t)] = {0};

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/*
 * The persist store is durable (TIKU_DURABLE), so entry metadata (key,
 * value_len, write_count, magic, valid) survives a power cycle, and the pool
 * sits at a fixed address, so a recovered fram_ptr stays valid for the same
 * image.  At boot tiku_persist_init() keeps the entries with a valid magic
 * and clears the rest; a virgin store has none.  tiku_lc_persist_register()
 * reuses a recovered key's slot without registering it again, so its
 * value_len and write_count are kept.
 *
 * The functions that modify entries (init, register, write, delete) hold an
 * MPU unlock window, since the store is write-protected durable memory;
 * reads need none.
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
 * Scans the durable store for magic-validated entries that
 * survived a power cycle.  Invalid entries are cleared.
 * Later calls do nothing.
 */
void tiku_lc_persist_init(void)
{
    uint16_t mpu_state;
    tiku_mem_arch_size_t i;

    if (lc_persist_initialized) {
        return;
    }

    /* tiku_persist_init() keeps the entries with a valid magic, which a
     * previous boot left, and clears the others. */
    mpu_state = tiku_mpu_unlock_nvm();
    tiku_persist_init(&lc_persist_store);
    tiku_mpu_lock_nvm(mpu_state);

    /* Each recovered entry's fram_ptr is an lc_t-sized chunk of
     * lc_nvm_pool.  Allocation resumes just past the highest one, so no
     * new key is handed a recovered key's slot. */
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
 * @brief Register a persistent LC slot under a key.
 *
 * Claims the next chunk of the pool and registers it.  An existing key, from
 * this boot or recovered at init, is left as it is, slot and value.
 *
 * @param key  Null-terminated key (at most TIKU_PERSIST_MAX_KEY_LEN - 1 chars)
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

    /* A key with an entry, from this boot or recovered at init, keeps it:
     * its fram_ptr is already in lc_nvm_pool below lc_nvm_next_slot, and
     * its write_count is kept.  Any read result but NOT_FOUND counts as an
     * entry, including a value too wide for the 1-byte probe. */
    if (tiku_persist_read(&lc_persist_store, key,
                          &probe, sizeof(probe), &probe_len)
        != TIKU_MEM_ERR_NOT_FOUND) {
        return 0;
    }

    if (lc_nvm_next_slot >= TIKU_LC_PERSIST_MAX_SLOTS) {
        return -2;
    }

    slot_buf = &lc_nvm_pool[lc_nvm_next_slot * sizeof(lc_t)];

    /* Register writes the durable entry metadata. */
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
 * @brief Save an lc_t value to NVM.
 *
 * Writes the continuation line, updating both the data slot and the durable
 * entry metadata, inside an MPU unlock window of its own: LC_SET_PERSISTENT
 * calls it with NVM locked.
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
 * @brief Load an lc_t value from NVM.
 *
 * Returns -1 when the key is unknown or holds no value (first boot, after a
 * clear) or holds 0 (after a reset); LC_RESUME_PERSISTENT then starts at
 * case 0.  Opens no MPU window.
 *
 * @param key  Key previously registered with tiku_lc_persist_register
 * @param val  Output: the stored lc_t value, 0 after a reset; unchanged when
 *             the key is unknown or holds no value
 * @return 0 on success, -1 if not found or empty
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
 * Deletes the key's entry, inside an MPU unlock window of its own, since the
 * entry metadata is write-protected durable memory.  The pool slot is not
 * freed: lc_nvm_next_slot only grows for the rest of the boot.
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
 * @brief Reset the NVM value to 0 without deleting the entry.
 *
 * A load then reports the key as not set, and the key stays registered, so
 * later saves succeed.  Opens its own MPU unlock window.
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
