/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_persist.c - persistent NVM key-value store implementation.
 *
 * Maps short string keys to NVM-backed buffers and implements the persist
 * cells.  Value copies go through the NVM HAL; gate words, the u32 fast path
 * and entry metadata are direct stores inside the NVM window.
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
 * @brief Find an entry by key (linear scan).
 *
 * Scans the valid entries for a matching key; the store holds at most
 * TIKU_PERSIST_MAX_ENTRIES (default 16).
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
 * @brief Initialize the persistent store, recovering valid entries.
 *
 * Scans every slot, keeping entries whose magic and valid flag are both set
 * and zeroing the rest, so the arbitrary contents of a virgin or reused store
 * become empty slots.
 *
 * @param store   Store to initialize
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if store is NULL,
 *         or TIKU_MEM_ERR_IO when the relock flush fails
 * @note Call once at boot.
 */
tiku_mem_err_t tiku_persist_init(tiku_persist_store_t *store)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    tiku_mem_arch_size_t i;
    tiku_mem_arch_size_t count;
    tiku_mem_err_t status;

    if (store == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* The store API owns its MPU windows, as the cell API does: the
     * store struct and value buffers commonly live in the protected
     * .persistent/.uninit region, where a store outside the window
     * faults or is dropped.  Nest-safe under callers holding their own
     * window. */
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

        status = tiku_mpu_lock_nvm_status(mpu_saved);
        tiku_atomic_exit();
    }

    return status;
}

/**
 * @brief Register an NVM buffer under a key.
 *
 * An existing key keeps its length and write count and takes the new pointer
 * and capacity; the value bytes are not copied, so they carry over only when
 * the buffer keeps its address.  A new key takes the first empty slot.
 *
 * @param store     Store to register into
 * @param key       Null-terminated key string
 * @param fram_buf  Pointer to caller-provided NVM buffer
 * @param capacity  Size of the NVM buffer in bytes
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID, TIKU_MEM_ERR_FULL, or
 *         TIKU_MEM_ERR_IO when the relock flush fails
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
     * the NUL.  persist_find() compares TIKU_PERSIST_MAX_KEY_LEN chars of the
     * caller's full key, so a truncated key would never match again. */
    if (strlen(key) >= TIKU_PERSIST_MAX_KEY_LEN) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* The buffer must sit in an NVM region. */
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

        tiku_mem_err_t status = tiku_mpu_lock_nvm_status(mpu_saved);
        tiku_atomic_exit();
        return status == TIKU_MEM_OK ? err : status;
    }
}

/**
 * @brief Read a value from the persistent store into an SRAM buffer.
 *
 * Copies out through the HAL.  NVM may carry wait states, so an SRAM copy is
 * faster to work with afterwards and is unaffected by a concurrent write.
 *
 * @param store     Store to read from
 * @param key       Key to look up
 * @param buf       Destination buffer in SRAM
 * @param buf_size  Size of @p buf in bytes
 * @param out_len   Output: the stored value's length, set even when @p buf
 *                  is too small
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, TIKU_MEM_ERR_NOMEM when
 *         @p buf is smaller than the value, or TIKU_MEM_ERR_INVALID
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
 * @brief Write a value from SRAM into the persistent NVM store.
 *
 * Copies through the HAL, records the length and bumps write_count, which
 * tiku_persist_wear_check() compares with TIKU_PERSIST_WEAR_THRESHOLD.
 *
 * @param store     Store to write into
 * @param key       Key to look up
 * @param data      Source data in SRAM
 * @param data_len  Length of source data
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, TIKU_MEM_ERR_NOMEM,
 *         TIKU_MEM_ERR_INVALID, or TIKU_MEM_ERR_IO when the relock flush
 *         fails
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

        tiku_mem_err_t status = tiku_mpu_lock_nvm_status(mpu_saved);
        tiku_atomic_exit();
        return status;
    }
}

/**
 * @brief Delete an entry from the persistent store.
 *
 * Zeroes the entry slot, so a later lookup of the key returns
 * TIKU_MEM_ERR_NOT_FOUND.
 *
 * @param store   Store to delete from
 * @param key     Key to delete
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, TIKU_MEM_ERR_INVALID, or
 *         TIKU_MEM_ERR_IO when the relock flush fails
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

        tiku_mem_err_t status = tiku_mpu_lock_nvm_status(mpu_saved);
        tiku_atomic_exit();
        return status;
    }
}

/**
 * @brief Check wear level for a key.
 *
 * Returns the write count and whether it has reached the warning threshold,
 * since NVM technologies have finite write endurance.
 *
 * @param store       Store to query
 * @param key         Key to check
 * @param write_count Output: number of writes to this key (may be NULL)
 * @return 1 once write_count reaches TIKU_PERSIST_WEAR_THRESHOLD, 0 below
 *         it, or a negative tiku_mem_err_t on error
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
/* PERSISTENT CELLS                                                          */
/*---------------------------------------------------------------------------*/

/*
 * See TIKU_PERSIST_CELL in tiku_mem.h.  The functions below implement the
 * magic-gate / MPU-window / commit-ordering idiom that the boot counter,
 * lifetime accumulator, device name, RTC epoch and the other cells use.
 *
 * NVM access routing: variable-length data copies go through the
 * tiku_mem_arch_nvm_write() HAL so platforms with per-range write
 * hooks (ECC scrub, cache maintenance) see every cell write; the
 * opaque cross-TU call also acts as a compiler barrier that pins
 * the data-before-gate store order.  The word-sized stores (gate
 * stamps, the write_u32 fast path) stay direct: they are the
 * atomicity-critical stores, and a single aligned word store is
 * power-cut-atomic where the HAL's byte loop is not.  Durability is
 * platform-owned either way: the relock (tiku_mpu_lock_nvm_status())
 * calls tiku_mem_arch_nvm_flush_status(), which commits everything
 * written inside the window (a mirror snapshot on the mirror platforms,
 * the MRAM write buffer on RA8P1); direct stores and HAL writes are
 * equally covered.
 */

/** Zero source for the chunked zero-fill through the NVM HAL */
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
 * Of those, cells primed (gate mismatch), served by /sys/persist/primed.
 * Non-zero on an established device means cells the image adds or could
 * not carry across a layout change, an NVM wipe, or corruption.
 */
static uint8_t cell_primed;

/* A value wider than one aligned arch-word store can tear on a power
 * cut (16-bit words on MSP430, 32-bit elsewhere).  For those, cell_write/
 * cell_commit run the crash-consistent protocol: invalidate the gate,
 * write the value, revalidate.  A cut mid-value then leaves an invalid
 * gate, and the next boot re-primes the default.  Single-word values skip
 * the protocol: the store itself is the atom.
 *
 * On the mirror platforms (Ambiq, RP2350, STM32N6, ESP32-C61) all three
 * steps land in SRAM inside one unlock window and only the final state
 * reaches the NVM mirror at relock; there the equivalent hole is a torn
 * flush, which the mirror's V2 CRC (tiku_nvm_mirror.h) detects at boot
 * restore.  Where `.persistent` is written in place (MSP430 FRAM, nRF54L
 * RRAM, RA8P1 MRAM) each step is durable on its own and the protocol alone
 * prevents the tear. */
#define CELL_CAN_TEAR(len)  ((len) > sizeof(unsigned int))

/**
 * @brief Validate a cell's gate; prime defaults on a virgin NVM.
 *
 * The value bytes are fully written before the gate is stamped, so a cut
 * anywhere in the window leaves an invalid gate and the next boot re-primes.
 * A stamped gate over half-written defaults cannot occur.
 *
 * @param c  Cell descriptor (from TIKU_PERSIST_CELL)
 * @return 1 when the cell was primed this boot, 0 when the persisted
 *         value was kept
 */
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
     * Data flows through the NVM HAL; the gate is one direct aligned
     * word store, which a power cut cannot tear. */
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
    /* Unchecked: init reports priming, not completion. */
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
 * A value wider than one arch word runs invalidate, write, revalidate, so a cut
 * mid-write leaves an invalid gate and the default re-primes next boot.
 * Single-word values are written directly and the gate is left as it was.
 *
 * @param c    Cell descriptor
 * @param src  New value bytes
 * @param len  Bytes to copy (clamped to the cell size)
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID on a NULL argument, or
 *         TIKU_MEM_ERR_IO when the relock flush fails
 */
tiku_mem_err_t tiku_persist_cell_write_status(const tiku_persist_cell_t *c,
                                              const void *src, uint16_t len)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    uint16_t saved;
    tiku_mem_err_t status;

    if (c == NULL || src == NULL || c->data == NULL || c->gate == NULL) {
        return TIKU_MEM_ERR_INVALID;
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
    status = tiku_mpu_lock_nvm_status(saved);
    tiku_atomic_exit();
    return status;
}

/**
 * @brief Update a cell's value, then stamp the gate (in that order).
 *
 * @param c    Cell descriptor
 * @param src  New value bytes
 * @param len  Bytes to copy (clamped to the cell size)
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID on a NULL argument, or
 *         TIKU_MEM_ERR_IO when the relock flush fails
 */
tiku_mem_err_t tiku_persist_cell_commit_status(const tiku_persist_cell_t *c,
                                               const void *src, uint16_t len)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    uint16_t saved;
    tiku_mem_err_t status;

    if (c == NULL || src == NULL || c->data == NULL || c->gate == NULL) {
        return TIKU_MEM_ERR_INVALID;
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
    status = tiku_mpu_lock_nvm_status(saved);
    tiku_atomic_exit();
    return status;
}

/**
 * @brief Convenience word write for uint32_t cells.
 *
 * When the cell is exactly a uint32_t (the macro guarantees natural
 * alignment, since the caller declared the variable), this compiles
 * to direct stores: one on 32-bit parts, two 16-bit words on MSP430.
 *
 * @param c  Cell descriptor; a cell of another size takes the first
 *           min(size, 4) bytes of @p v through the NVM HAL, gated as
 *           tiku_persist_cell_write() gates a value of that length
 * @param v  New value
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID on a NULL argument, or
 *         TIKU_MEM_ERR_IO when the relock flush fails
 */
tiku_mem_err_t tiku_persist_cell_write_u32_status(const tiku_persist_cell_t *c,
                                                  uint32_t v)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    uint16_t saved;
    tiku_mem_err_t status;

    if (c == NULL || c->data == NULL || c->gate == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    tiku_atomic_enter();
    saved = tiku_mpu_unlock_nvm();
    if (c->size == sizeof(uint32_t)) {
        /* A direct aligned store is power-cut-atomic at word
         * granularity on 32-bit parts.  On MSP430 a uint32_t is two
         * 16-bit word stores and can tear, so the gate protocol
         * brackets it there (CELL_CAN_TEAR(4) is false on 32-bit
         * parts). */
        if (CELL_CAN_TEAR(sizeof(uint32_t))) {
            *c->gate = 0;
        }
        *(uint32_t *)c->data = v;
        if (CELL_CAN_TEAR(sizeof(uint32_t))) {
            *c->gate = c->key;
        }
    } else {
        uint16_t n = (c->size < sizeof(uint32_t))
                         ? c->size : (uint16_t)sizeof(uint32_t);

        /* The same tear rule as tiku_persist_cell_write_status(). */
        if (CELL_CAN_TEAR(n)) {
            *c->gate = 0;
        }
        tiku_mem_arch_nvm_write((uint8_t *)c->data, (const uint8_t *)&v, n);
        if (CELL_CAN_TEAR(n)) {
            *c->gate = c->key;
        }
    }
    status = tiku_mpu_lock_nvm_status(saved);
    tiku_atomic_exit();
    return status;
}

/** @brief tiku_persist_cell_write_status() with the status dropped. */
void tiku_persist_cell_write(const tiku_persist_cell_t *c,
                             const void *src, uint16_t len)
{
    (void)tiku_persist_cell_write_status(c, src, len);
}

/** @brief tiku_persist_cell_commit_status() with the status dropped. */
void tiku_persist_cell_commit(const tiku_persist_cell_t *c,
                              const void *src, uint16_t len)
{
    (void)tiku_persist_cell_commit_status(c, src, len);
}

/** @brief tiku_persist_cell_write_u32_status() with the status dropped. */
void tiku_persist_cell_write_u32(const tiku_persist_cell_t *c, uint32_t v)
{
    (void)tiku_persist_cell_write_u32_status(c, v);
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
