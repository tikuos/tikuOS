/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_init.c - NVM-backed configurable boot (init system).
 *
 * Replays an ordered table of shell commands stored in the NVM config region,
 * so the startup sequence is reconfigurable without recompiling.  Every
 * mutation is bracketed by the MPU unlock/lock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_init.h"
#include "tiku.h"
#include <kernel/memory/tiku_nvm_map.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/shell/tiku_shell_parser.h>
#include <kernel/shell/tiku_shell.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/*
 * Magic word distinguishing an initialised init region from blank NVM.  Written
 * last by init_commit() and checked by tiku_init_load(); a mismatch means fresh
 * or wiped storage, or an interrupted replace or remove, and triggers a prime.
 * Bump it if the layout below changes incompatibly, so old images re-prime.
 */
#define TIKU_INIT_MAGIC  0x1417U

/*---------------------------------------------------------------------------*/
/* NVM LAYOUT                                                                */
/*---------------------------------------------------------------------------*/

/*
 * The NVM config region is laid out as:
 *
 *   [0..1]   uint16_t magic
 *   [2]      uint8_t  count   (number of populated entries)
 *   [3]      uint8_t  reserved
 *   [4..]    tiku_init_entry_t entries[TIKU_INIT_MAX_ENTRIES]
 */

/*
 * Byte offsets of each field within the config region.  These must stay
 * consistent with the layout above: init_read_magic(), init_read_count() and
 * init_entry_ptr() index the region through exactly these constants.
 */
#define OFF_MAGIC    0
#define OFF_COUNT    2
#define OFF_ENTRIES  4

/*
 * Total bytes the layout consumes: the 4-byte header plus the entry array.  A
 * live sizeof() expression rather than a computed literal, so it tracks any
 * change to the entry count or struct and feeds the static assert below.
 */
#define TIKU_INIT_REGION_BYTES_NEEDED \
    (OFF_ENTRIES + (TIKU_INIT_MAX_ENTRIES * sizeof(tiku_init_entry_t)))

/*
 * Compile-time guard: the device's CONFIG region must be at least as
 * large as the init table needs.
 *
 * tiku_init_load() disables the table at run time when the region is
 * smaller than TIKU_INIT_REGION_BYTES_NEEDED; the assert makes that a
 * build error instead.  It applies only where the device defines
 * TIKU_DEVICE_FRAM_CONFIG_SIZE, so ports that size the CONFIG region
 * another way still compile.
 */
#if defined(TIKU_DEVICE_FRAM_CONFIG_SIZE)
_Static_assert(TIKU_DEVICE_FRAM_CONFIG_SIZE >= TIKU_INIT_REGION_BYTES_NEEDED,
    "TIKU_DEVICE_FRAM_CONFIG_SIZE is too small for the init table; "
    "tiku_init_load() would disable it at boot. "
    "Bump the device's TIKU_DEVICE_FRAM_CONFIG_SIZE to >= "
    "(4 + TIKU_INIT_MAX_ENTRIES * sizeof(tiku_init_entry_t)).");
#endif

/*---------------------------------------------------------------------------*/
/* INTERNAL STATE                                                            */
/*---------------------------------------------------------------------------*/

/*
 * Cached base address of the NVM config region, resolved once by
 * tiku_init_load() and reused as the anchor for every OFF_* offset.  NULL until
 * that succeeds, and every public function guards on it, so an early call is a
 * no-op rather than a NULL dereference.
 */
static uint8_t *cfg_base;

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the magic word from the NVM config region.
 *
 * Uses memcpy to avoid unaligned-access faults on platforms that
 * require aligned reads.  No MPU unlock is needed — NVM is always
 * readable.
 *
 * @return The 16-bit magic value stored at offset OFF_MAGIC.
 */
static uint16_t
init_read_magic(void)
{
    uint16_t val;
    memcpy(&val, cfg_base + OFF_MAGIC, sizeof(val));
    return val;
}

/**
 * @brief Read the entry count byte from the NVM config region.
 *
 * @return Number of populated init entries (0 .. TIKU_INIT_MAX_ENTRIES).
 */
static uint8_t
init_read_count(void)
{
    return *(cfg_base + OFF_COUNT);
}

/**
 * @brief Return a pointer to the idx-th init entry in NVM.
 *
 * Calculates the byte offset into the config region for entry @p idx.
 * The caller is responsible for bounds-checking idx < count.
 *
 * @param idx  Zero-based entry index.
 * @return     Pointer to the entry (inside the NVM config region).
 */
static tiku_init_entry_t *
init_entry_ptr(uint8_t idx)
{
    return (tiku_init_entry_t *)(cfg_base + OFF_ENTRIES +
           (uint16_t)idx * sizeof(tiku_init_entry_t));
}

/** @brief Whether the stored table is well formed; checked before any use. */
static uint8_t
init_table_valid(void)
{
    uint8_t i, count;

    if (cfg_base == NULL || init_read_magic() != TIKU_INIT_MAGIC) {
        return 0;
    }
    count = init_read_count();
    if (count > TIKU_INIT_MAX_ENTRIES) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const tiku_init_entry_t *e = init_entry_ptr(i);
        if (e->enabled > 1 ||
            memchr(e->name, '\0', sizeof(e->name)) == NULL ||
            memchr(e->cmd, '\0', sizeof(e->cmd)) == NULL) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Write @p len bytes to NVM.
 *
 * Thin wrapper around tiku_mem_arch_nvm_write() with casts for
 * convenience.
 *
 * @note The caller holds the MPU unlocked.
 *
 * @param fram_ptr  Destination address in NVM.
 * @param sram_ptr  Source address in SRAM.
 * @param len       Number of bytes to write.
 */
#define INIT_NVM_WRITE(fram_ptr, sram_ptr, len) \
    tiku_mem_arch_nvm_write((uint8_t *)(fram_ptr), \
                            (const uint8_t *)(sram_ptr), (len))

/**
 * @brief Clear the magic before an edit that rewrites a live entry.
 *
 * Replace, remove and the first-boot prime call this first, so a power loss
 * empties the table instead of leaving a torn or duplicated command to run.
 *
 * @return 0 once the cleared magic reads back, -1 otherwise
 */
static int8_t
init_invalidate(void)
{
    uint16_t zero = 0;
    uint16_t saved = tiku_mpu_unlock_nvm();
    INIT_NVM_WRITE(cfg_base + OFF_MAGIC, &zero, sizeof(zero));
    if (tiku_mpu_lock_nvm_status(saved) != TIKU_MEM_OK) {
        return -1;
    }
    return init_read_magic() == 0 ? 0 : -1;
}

/**
 * @brief Close a write window: 0 if its flush landed and the table checks out.
 *
 * Protection is restored on every path.
 */
static int8_t
init_lock(uint16_t saved)
{
    if (tiku_mpu_lock_nvm_status(saved) != TIKU_MEM_OK) {
        return -1;
    }
    return init_table_valid() ? 0 : -1;
}

/**
 * @brief Flush the entries, then publish their magic.
 *
 * A failed flush leaves the magic cleared and returns -1.
 */
static int8_t
init_commit(uint16_t saved)
{
    uint16_t magic = TIKU_INIT_MAGIC;
    if (tiku_mpu_lock_nvm_status(saved) != TIKU_MEM_OK) {
        return -1;
    }
    saved = tiku_mpu_unlock_nvm();
    INIT_NVM_WRITE(cfg_base + OFF_MAGIC, &magic, sizeof(magic));
    return init_lock(saved);
}

/**
 * @brief Compare two entry names as tiku_init_add() stores them.
 *
 * Only the first TIKU_INIT_NAME_SIZE-1 characters count, so a name longer
 * than that matches the truncated entry it was added as.
 *
 * @param a  First string.
 * @param b  Second string.
 * @return   1 if equal, 0 if different.
 */
static uint8_t
init_name_match(const char *a, const char *b)
{
    return strncmp(a, b, TIKU_INIT_NAME_SIZE - 1) == 0;
}

/**
 * @brief Find the index of an init entry by name.
 *
 * Performs a linear scan of all populated entries.  The scan is
 * bounded by TIKU_INIT_MAX_ENTRIES (typically 8), so the cost is
 * negligible.
 *
 * @param name  Entry name to search for (NUL-terminated).
 * @return      Index (0 .. count-1) on match, or -1 if not found.
 */
static int8_t
init_find(const char *name)
{
    uint8_t i;
    uint8_t count = init_read_count();

    for (i = 0; i < count; i++) {
        const tiku_init_entry_t *e = init_entry_ptr(i);
        if (init_name_match(e->name, name)) {
            return (int8_t)i;
        }
    }
    return -1;
}

/**
 * @brief Initialise NVM on first boot (zero everything, write magic).
 *
 * Called when the stored table is blank or fails the checks.  Zeros the count,
 * the reserved byte and every entry slot, then writes the magic last as the
 * commit marker, so a power loss mid-prime leaves the region uninitialised.
 */
static void
init_first_boot(void)
{
    uint16_t saved;
    uint8_t  zero  = 0;
    uint8_t  zbuf[sizeof(tiku_init_entry_t)];
    uint8_t  i;

    memset(zbuf, 0, sizeof(zbuf));

    if (init_invalidate() != 0) {
        return;
    }

    saved = tiku_mpu_unlock_nvm();

    INIT_NVM_WRITE(cfg_base + OFF_COUNT, &zero, 1);
    INIT_NVM_WRITE(cfg_base + OFF_COUNT + 1, &zero, 1);    /* reserved */

    for (i = 0; i < TIKU_INIT_MAX_ENTRIES; i++) {
        INIT_NVM_WRITE(init_entry_ptr(i), zbuf, sizeof(zbuf));
    }

    (void)init_commit(saved);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Resolve the config region and validate (or prime) the table.
 *
 * Caches the region base and primes an empty table when the stored one is
 * blank or fails the checks.  A missing or undersized region, or a prime whose
 * flush fails, leaves the base NULL and disables every other entry point.
 *
 * @note Call once at boot, after the NVM map and before run_all().
 */
void
tiku_init_load(void)
{
    const tiku_nvm_region_t *r;

    cfg_base = NULL;
    r = tiku_nvm_region_get(TIKU_NVM_REGION_CONFIG);
    if (r == NULL || r->base == NULL ||
        r->size < TIKU_INIT_REGION_BYTES_NEEDED) {
        return;
    }

    cfg_base = r->base;

    /* A table this build cannot hold (a count past TIKU_INIT_MAX_ENTRIES, an
     * unterminated string) is an incompatible layout, like a changed magic. */
    if (!init_table_valid()) {
        init_first_boot();
    }
    if (!init_table_valid()) {
        cfg_base = NULL;
    }
}

/**
 * @brief Execute every enabled entry, ordered by sequence number.
 *
 * Insertion-sorts an index array by seq, then dispatches each enabled,
 * non-empty command from an SRAM copy: the parser tokenises in place and must
 * not write into the read-only NVM image.  No NVM writes happen here.
 *
 * @return Number of entries actually executed (enabled, non-empty).
 */
uint8_t
tiku_init_run_all(void)
{
    uint8_t count;
    uint8_t executed = 0;
    uint8_t i;
    uint8_t j;
    uint8_t order[TIKU_INIT_MAX_ENTRIES];
    char scratch[TIKU_INIT_CMD_SIZE];

    if (!init_table_valid()) {
        return 0;
    }

    count = init_read_count();
    if (count == 0) {
        return 0;
    }

    /* Index sorted by seq: an insertion sort over at most
     * TIKU_INIT_MAX_ENTRIES entries. */
    for (i = 0; i < count; i++) {
        order[i] = i;
    }
    for (i = 1; i < count; i++) {
        uint8_t key = order[i];
        uint8_t key_seq = init_entry_ptr(key)->seq;
        j = i;
        while (j > 0 && init_entry_ptr(order[j - 1])->seq > key_seq) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }

    /* Execute each enabled entry via the shell parser */
    for (i = 0; i < count; i++) {
        const tiku_init_entry_t *e = init_entry_ptr(order[i]);

        if (!e->enabled) {
            continue;
        }
        if (e->cmd[0] == '\0') {
            continue;
        }

        /* Copy to SRAM scratch — parser modifies the buffer in-place */
        memset(scratch, 0, sizeof(scratch));
        strncpy(scratch, e->cmd, TIKU_INIT_CMD_SIZE - 1);

        TIKU_PRINTF("[init] %02u %s: %s\n", e->seq, e->name, scratch);
        tiku_shell_parser_execute(scratch);
        executed++;
    }

    return executed;
}

/**
 * @brief Add or replace an init entry.
 *
 * A new name is written into the slot past the count and the count is bumped
 * after it, so a power loss leaves the old table.  An existing name is
 * rewritten in place between init_invalidate() and init_commit().
 *
 * @param seq   Boot sequence number; lower runs earlier.
 * @param name  Entry name, truncated to TIKU_INIT_NAME_SIZE-1 chars.
 * @param cmd   Shell command, truncated to TIKU_INIT_CMD_SIZE-1 chars.
 * @return 0 on success (added or replaced); -1 if the table is unusable, an
 *         argument is NULL, the table is full on add, or the flush fails.
 */
int8_t
tiku_init_add(uint8_t seq, const char *name, const char *cmd)
{
    int8_t idx;
    uint8_t count;
    uint16_t saved;
    tiku_init_entry_t entry;

    if (!init_table_valid() || name == (const char *)0 ||
        cmd == (const char *)0) {
        return -1;
    }

    memset(&entry, 0, sizeof(entry));
    entry.seq = seq;
    entry.enabled = 1;
    strncpy(entry.name, name, TIKU_INIT_NAME_SIZE - 1);
    strncpy(entry.cmd, cmd, TIKU_INIT_CMD_SIZE - 1);

    idx = init_find(name);
    count = init_read_count();

    if (idx >= 0) {
        if (init_invalidate() != 0) {
            return -1;
        }
        saved = tiku_mpu_unlock_nvm();
        INIT_NVM_WRITE(init_entry_ptr((uint8_t)idx), &entry, sizeof(entry));
        return init_commit(saved);
    }

    if (count >= TIKU_INIT_MAX_ENTRIES) {
        return -1;
    }

    saved = tiku_mpu_unlock_nvm();
    INIT_NVM_WRITE(init_entry_ptr(count), &entry, sizeof(entry));
    count++;
    INIT_NVM_WRITE(cfg_base + OFF_COUNT, &count, 1);
    return init_lock(saved);
}

/**
 * @brief Remove an init entry by name.
 *
 * Moves the last entry into the freed slot, zeroes the tail slot and
 * decrements the count, between init_invalidate() and init_commit().  Order in
 * NVM is irrelevant because run_all() re-sorts by seq every boot.
 *
 * @param name  Name of the entry to remove (NUL-terminated).
 * @return 0 on success; -1 if the table is unusable, @p name is NULL, no
 *         entry matches, or the flush fails.
 */
int8_t
tiku_init_remove(const char *name)
{
    int8_t idx;
    uint8_t count;
    uint16_t saved;

    if (!init_table_valid() || name == (const char *)0) {
        return -1;
    }

    idx = init_find(name);
    if (idx < 0) {
        return -1;
    }

    count = init_read_count();

    if (init_invalidate() != 0) {
        return -1;
    }
    saved = tiku_mpu_unlock_nvm();

    /* Move the last entry into the removed slot; run_all() sorts by seq. */
    if ((uint8_t)idx < count - 1) {
        tiku_init_entry_t *last = init_entry_ptr(count - 1);
        INIT_NVM_WRITE(init_entry_ptr((uint8_t)idx), last,
                       sizeof(tiku_init_entry_t));
    }

    /* Zero the now-unused last slot */
    {
        uint8_t zbuf[sizeof(tiku_init_entry_t)];
        memset(zbuf, 0, sizeof(zbuf));
        INIT_NVM_WRITE(init_entry_ptr(count - 1), zbuf, sizeof(zbuf));
    }

    count--;
    INIT_NVM_WRITE(cfg_base + OFF_COUNT, &count, 1);

    return init_commit(saved);
}

/**
 * @brief Enable or disable an init entry by name.
 *
 * Writes just the entry's enabled byte, leaving seq, name and cmd untouched.  A
 * disabled entry stays in the table and in the count but is skipped by
 * tiku_init_run_all().
 *
 * @param name  Name of the entry to toggle (NUL-terminated).
 * @param en    Non-zero to enable, zero to disable.
 * @return 0 on success; -1 if the table is unusable, @p name is NULL, no
 *         entry matches, or the flush fails.
 */
int8_t
tiku_init_enable(const char *name, uint8_t en)
{
    int8_t idx;
    uint16_t saved;
    uint8_t val;

    if (!init_table_valid() || name == (const char *)0) {
        return -1;
    }

    idx = init_find(name);
    if (idx < 0) {
        return -1;
    }

    val = en ? 1 : 0;

    saved = tiku_mpu_unlock_nvm();
    INIT_NVM_WRITE(&(init_entry_ptr((uint8_t)idx)->enabled), &val, 1);
    return init_lock(saved);
}

/**
 * @brief Return the number of entries currently in the table.
 *
 * Counts both enabled and disabled entries (the on-NVM count byte
 * tracks populated slots, not just active ones).  Read-only, no MPU
 * interaction.
 *
 * @return Entry count, or 0 if the table is unusable.
 */
uint8_t
tiku_init_count(void)
{
    if (!init_table_valid()) {
        return 0;
    }
    return init_read_count();
}

/**
 * @brief Get a read-only pointer to the idx-th entry.
 *
 * Bounds-checks against the live count and returns a pointer straight into NVM,
 * with no copy.  Callers may read but must not write through it; use add,
 * remove or enable to mutate.
 *
 * @param idx  Zero-based index, valid range 0 .. tiku_init_count()-1.
 * @return Pointer to the entry, or NULL if the table is unusable or
 *         @p idx is out of range.
 */
const tiku_init_entry_t *
tiku_init_get(uint8_t idx)
{
    if (!init_table_valid() || idx >= init_read_count()) {
        return (const tiku_init_entry_t *)0;
    }
    return init_entry_ptr(idx);
}
