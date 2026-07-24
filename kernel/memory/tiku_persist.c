/*
 * Tiku Operating System v0.06
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
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"
#include "hal/tiku_cpu.h"
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
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    tiku_mem_arch_size_t i;
    tiku_mem_arch_size_t count;

    if (store == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* The store API owns its MPU windows (same doctrine as the cell
     * API): the store struct and value buffers commonly live in the
     * protected .persistent/.uninit region, and with real write
     * protection an un-windowed store op is a MemManage fault --
     * found exactly that way (test_nvm_pool, hardfault @0x2000433c).
     * Nest-safe under callers holding their own window. */
    {
        uint16_t mpu_saved;

        tiku_atomic_enter();
        mpu_saved = tiku_mpu_unlock_nvm();

        count = 0;
        for (i = 0; i < TIKU_PERSIST_MAX_ENTRIES; i++) {
            if (store->entries[i].magic == TIKU_PERSIST_MAGIC &&
                store->entries[i].valid) {
                count++;
            } else {
                memset(&store->entries[i], 0,
                       sizeof(tiku_persist_entry_t));
            }
        }
        store->count = count;

        tiku_mpu_lock_nvm(mpu_saved);
        tiku_atomic_exit();
    }

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
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    tiku_persist_entry_t *entry;
    tiku_mem_arch_size_t i;

    if (store == NULL || key == NULL || fram_buf == NULL || capacity == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Reject keys that do not fit key[TIKU_PERSIST_MAX_KEY_LEN] including
     * the NUL.  Silent truncation used to store a 7-char prefix that
     * persist_find (which compares TIKU_PERSIST_MAX_KEY_LEN chars of the
     * caller's FULL key) could never match again -- the entry registered
     * fine and then every write/read/delete under the same key returned
     * NOT_FOUND (bit on nRF54LM20A HW via the persist-reset-survival
     * test's 9-char "tb.reboot", 2026-07-14).  The persist edge test
     * documents reject-with-INVALID as sanctioned behavior. */
    if (strlen(key) >= TIKU_PERSIST_MAX_KEY_LEN) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Verify the FRAM buffer resides in NVM */
    if (!tiku_region_contains(fram_buf, capacity, TIKU_MEM_REGION_NVM)) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Self-windowed (see tiku_persist_init): the entry slots may live
     * in the protected region. */
    {
        uint16_t mpu_saved;
        tiku_mem_err_t err = TIKU_MEM_ERR_FULL;

        tiku_atomic_enter();
        mpu_saved = tiku_mpu_unlock_nvm();

        /* If key already exists, update pointer but preserve data */
        entry = persist_find(store, key);
        if (entry != NULL) {
            entry->fram_ptr = fram_buf;
            entry->capacity = capacity;
            err = TIKU_MEM_OK;
        } else {
            /* Find first empty slot */
            for (i = 0; i < TIKU_PERSIST_MAX_ENTRIES; i++) {
                if (!store->entries[i].valid) {
                    entry = &store->entries[i];

                    memset(entry, 0, sizeof(tiku_persist_entry_t));
                    strncpy(entry->key, key,
                            TIKU_PERSIST_MAX_KEY_LEN - 1);
                    entry->key[TIKU_PERSIST_MAX_KEY_LEN - 1] = '\0';
                    entry->fram_ptr    = fram_buf;
                    entry->capacity    = capacity;
                    entry->value_len   = 0;
                    entry->write_count = 0;
                    entry->magic       = TIKU_PERSIST_MAGIC;
                    entry->valid       = 1;

                    store->count++;
                    err = TIKU_MEM_OK;
                    break;
                }
            }
        }

        tiku_mpu_lock_nvm(mpu_saved);
        tiku_atomic_exit();
        return err;
    }
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
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
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

    /* Self-windowed (see tiku_persist_init): the value buffer and the
     * store metadata may live in the protected region. */
    {
        uint16_t mpu_saved;

        tiku_atomic_enter();
        mpu_saved = tiku_mpu_unlock_nvm();

        tiku_mem_arch_nvm_write(entry->fram_ptr, data, data_len);
        entry->value_len = data_len;
        entry->write_count++;

        tiku_mpu_lock_nvm(mpu_saved);
        tiku_atomic_exit();
    }

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
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    tiku_persist_entry_t *entry;

    if (store == NULL || key == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    entry = persist_find(store, key);
    if (entry == NULL) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    /* Self-windowed (see tiku_persist_init). */
    {
        uint16_t mpu_saved;

        tiku_atomic_enter();
        mpu_saved = tiku_mpu_unlock_nvm();

        memset(entry, 0, sizeof(tiku_persist_entry_t));
        store->count--;

        tiku_mpu_lock_nvm(mpu_saved);
        tiku_atomic_exit();
    }

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

/*---------------------------------------------------------------------------*/
/* PERSISTENT CELLS — declared magic-gated NVM values                        */
/*---------------------------------------------------------------------------*/

/*
 * See the design discussion in tiku_mem.h.  The functions below are
 * the single implementation of the magic-gate / MPU-window / commit-
 * ordering idiom that the boot counter, lifetime accumulator, device
 * name and RTC epoch previously each hand-rolled.
 *
 * NVM access routing: variable-length data copies go through the
 * tiku_mem_arch_nvm_write() HAL so platforms with per-range write
 * hooks (ECC scrub, cache maintenance) see every cell write; the
 * opaque cross-TU call also acts as a compiler barrier that pins
 * the data-before-gate store order.  The two word-sized stores
 * (gate stamp, write_u32 fast path) stay direct ON PURPOSE: they
 * are the atomicity-critical stores, and a single aligned word
 * store is power-cut-atomic where the HAL's byte loop is not.
 * Durability is platform-owned either way — tiku_mpu_lock_nvm()
 * calls tiku_mem_arch_nvm_flush(), which commits everything written
 * inside the window (no-op on FRAM, flash-sector snapshot on
 * RP2350); direct stores and HAL writes are equally covered.
 */

/** Zero source for chunked default-fill through the NVM HAL */
static const uint8_t cell_zeros[16];

/**
 * @brief Zero-fill a cell's data through the NVM write HAL.
 *
 * @param c  Cell descriptor
 */
static void cell_zero_fill(const tiku_persist_cell_t *c)
{
    uint16_t off = 0;

    while (off < c->size) {
        uint16_t n = (uint16_t)(c->size - off);
        if (n > (uint16_t)sizeof(cell_zeros)) {
            n = (uint16_t)sizeof(cell_zeros);
        }
        tiku_mem_arch_nvm_write((uint8_t *)c->data + off,
                                cell_zeros, n);
        off = (uint16_t)(off + n);
    }
}

/**
 * Cells validated by tiku_persist_cell_init() since reset.  SRAM
 * (per-boot statistic); served by /sys/persist/cells.
 */
static uint8_t cell_count;

/**
 * Of those, cells that had to be primed (gate mismatch).  SRAM;
 * served by /sys/persist/primed.  Non-zero on an established device
 * signals a layout change, an NVM wipe, or in-field corruption.
 */
static uint8_t cell_primed;

/**
 * @brief Validate a cell's gate; prime defaults on a virgin NVM.
 *
 * The commit-ordering discipline lives here, once: the value bytes
 * (zero-fill, then the default) are fully written BEFORE the gate is
 * stamped, so a power cut anywhere inside the window leaves the gate
 * invalid and the next boot simply re-primes.  A stamped gate over
 * half-written defaults cannot occur.
 *
 * @param c  Cell descriptor (from TIKU_PERSIST_CELL)
 * @return 1 when the cell was primed this boot, 0 when the persisted
 *         value was kept
 */
/* A value wider than one aligned arch-word store can tear on a power
 * cut (16-bit words on MSP430, 32-bit on ARM).  For those, cell_write/
 * cell_commit run the crash-consistent protocol: INVALIDATE the gate,
 * write the value, REVALIDATE.  A cut mid-value then leaves an invalid
 * gate — the next boot re-primes the default — instead of a torn value
 * that a reader would trust.  Single-word values skip the protocol:
 * the store itself is the atom.
 *
 * Mirror-platform note (Ambiq/RP2350): inside one unlock window all
 * three steps land in SRAM and only the final state reaches the NVM
 * mirror at relock — there the equivalent hole is a TORN FLUSH, which
 * the mirror's V2 CRC (tiku_nvm_mirror.h) detects at boot restore.
 * On MSP430 (FRAM in place) each step is individually durable and the
 * protocol carries the full weight. */
#define CELL_CAN_TEAR(len)  ((len) > sizeof(unsigned int))

uint8_t tiku_persist_cell_init(const tiku_persist_cell_t *c)
{
    TIKU_MEM_KERNEL_ONLY(0);
    uint16_t saved;
    uint16_t n;

    if (c == NULL) {
        return 0;
    }

    cell_count++;

    if (*c->gate == c->key) {
        return 0;               /* persisted value is real — keep it */
    }

    /* Virgin (or corrupted) NVM: prime defaults, gate stamped last.
     * Data flows through the NVM HAL; the gate is one direct word
     * store (power-cut-atomic — see the routing note above). */
    tiku_atomic_enter();        /* an ISR inside the window would have
                                 * NVM write access — keep it closed  */
    saved = tiku_mpu_unlock_nvm();
    cell_zero_fill(c);
    if (c->def != NULL && c->def_size > 0) {
        n = (c->def_size > c->size) ? c->size : c->def_size;
        tiku_mem_arch_nvm_write((uint8_t *)c->data,
                                (const uint8_t *)c->def, n);
    }
    *c->gate = c->key;          /* commit point */
    tiku_mpu_lock_nvm(saved);
    tiku_atomic_exit();

    cell_primed++;
    return 1;
}

/**
 * @brief Report whether a cell's gate currently validates.
 *
 * @param c  Cell descriptor
 * @return Non-zero when the gate holds the key
 */
uint8_t tiku_persist_cell_valid(const tiku_persist_cell_t *c)
{
    return (c != NULL && *c->gate == c->key) ? 1u : 0u;
}

/**
 * @brief Update a cell's value (crash-consistently for wide values).
 *
 * Values wider than one arch word run invalidate->write->revalidate
 * (see CELL_CAN_TEAR): a power cut mid-write re-primes the default on
 * the next boot instead of leaving a torn value behind a valid gate.
 * Single-word values are written directly; the gate is untouched.
 *
 * @param c    Cell descriptor
 * @param src  New value bytes
 * @param len  Bytes to copy (clamped to the cell size)
 */
void tiku_persist_cell_write(const tiku_persist_cell_t *c,
                             const void *src, uint16_t len)
{
    TIKU_MEM_KERNEL_ONLY_VOID();
    uint16_t saved;

    if (c == NULL || src == NULL) {
        return;
    }
    if (len > c->size) {
        len = c->size;
    }

    tiku_atomic_enter();
    saved = tiku_mpu_unlock_nvm();
    if (CELL_CAN_TEAR(len)) {
        *c->gate = 0;           /* invalidate: a tear re-primes      */
    }
    tiku_mem_arch_nvm_write((uint8_t *)c->data,
                            (const uint8_t *)src, len);
    if (CELL_CAN_TEAR(len)) {
        *c->gate = c->key;      /* revalidate: value fully written   */
    }
    tiku_mpu_lock_nvm(saved);
    tiku_atomic_exit();
}

/**
 * @brief Update a cell's value, then stamp the gate (in that order).
 *
 * @param c    Cell descriptor
 * @param src  New value bytes
 * @param len  Bytes to copy (clamped to the cell size)
 */
void tiku_persist_cell_commit(const tiku_persist_cell_t *c,
                              const void *src, uint16_t len)
{
    TIKU_MEM_KERNEL_ONLY_VOID();
    uint16_t saved;

    if (c == NULL || src == NULL) {
        return;
    }
    if (len > c->size) {
        len = c->size;
    }

    tiku_atomic_enter();
    saved = tiku_mpu_unlock_nvm();
    if (CELL_CAN_TEAR(len)) {
        *c->gate = 0;           /* a previously-valid gate must not
                                 * survive a mid-value tear          */
    }
    tiku_mem_arch_nvm_write((uint8_t *)c->data,
                            (const uint8_t *)src, len);
    *c->gate = c->key;          /* commit point — after the data */
    tiku_mpu_lock_nvm(saved);
    tiku_atomic_exit();
}

/**
 * @brief Convenience word write for uint32_t cells.
 *
 * When the cell is exactly a uint32_t (the macro guarantees natural
 * alignment, since the caller declared the variable), this compiles
 * to direct stores — one on ARM, two 16-bit words on MSP430.
 *
 * @param c  Cell descriptor (size must be 4)
 * @param v  New value
 */
void tiku_persist_cell_write_u32(const tiku_persist_cell_t *c,
                                 uint32_t v)
{
    TIKU_MEM_KERNEL_ONLY_VOID();
    uint16_t saved;

    if (c == NULL) {
        return;
    }

    tiku_atomic_enter();
    saved = tiku_mpu_unlock_nvm();
    if (c->size == sizeof(uint32_t)) {
        /* Direct aligned store on purpose: power-cut-atomic at word
         * granularity on ARM.  On MSP430 a uint32_t is TWO 16-bit
         * word stores and can tear — bracket with the gate protocol
         * there (compile-time: CELL_CAN_TEAR(4) is false on ARM). */
        if (CELL_CAN_TEAR(sizeof(uint32_t))) {
            *c->gate = 0;
        }
        *(uint32_t *)c->data = v;
        if (CELL_CAN_TEAR(sizeof(uint32_t))) {
            *c->gate = c->key;
        }
    } else {
        tiku_mem_arch_nvm_write((uint8_t *)c->data, (const uint8_t *)&v,
                                (c->size < sizeof(uint32_t))
                                    ? c->size : (uint16_t)sizeof(uint32_t));
    }
    tiku_mpu_lock_nvm(saved);
    tiku_atomic_exit();
}

/**
 * @brief Number of cells validated by cell_init() this boot.
 *
 * @return Count of cell_init() calls since reset
 */
uint8_t tiku_persist_cell_count(void)
{
    return cell_count;
}

/**
 * @brief Number of cells that had to be primed this boot.
 *
 * @return Count of cell_init() calls that returned 1 since reset
 */
uint8_t tiku_persist_cell_primed(void)
{
    return cell_primed;
}
