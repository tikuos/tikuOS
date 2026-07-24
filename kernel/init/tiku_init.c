/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_init.c - NVM-backed configurable boot (init system)
 *
 * Stores an ordered table of shell-command "init entries" in
 * non-volatile memory and replays them at boot through the shell
 * parser, so the startup sequence can be reconfigured without
 * recompiling — the embedded analogue of /etc/init.d run through the
 * shell.
 *
 * The init table is stored inside a non-volatile memory (NVM) config
 * region obtained from the NVM region map.  A magic word detects
 * first-boot and auto-clears.  All NVM writes go through the
 * platform's memory-protection unlock / nvm_write / lock sequence.
 *
 * On-NVM layout (see the NVM LAYOUT section) is a magic word, a
 * one-byte entry count, a reserved byte, then a fixed array of
 * tiku_init_entry_t slots.  Reads are plain (NVM is always readable),
 * but every mutation — first-boot prime, add, remove, enable toggle —
 * is bracketed by tiku_mpu_unlock_nvm() / tiku_mpu_lock_nvm() because
 * the MPU write-protects NVM by default.  The magic word is always
 * written last on first boot so a power loss mid-prime leaves the
 * region detectably uninitialised rather than half-formed.
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

/**
 * Magic word distinguishing an initialised init region from blank
 * NVM.
 *
 * Written (last) at offset OFF_MAGIC by init_first_boot(); checked by
 * tiku_init_load().  A mismatch means the region was never primed —
 * fresh flash/FRAM or a wiped device — and triggers a one-time
 * first-boot prime.  The literal 0x1417 has no semantic meaning
 * beyond being an unlikely value for uninitialised memory; bump it if
 * the on-NVM layout below ever changes incompatibly so old images
 * re-prime cleanly after reflashing.
 */
#define TIKU_INIT_MAGIC  0x1417U

/*---------------------------------------------------------------------------*/
/* NVM LAYOUT                                                                */
/*---------------------------------------------------------------------------*/

/**
 * The NVM config region is laid out as:
 *
 *   [0..1]   uint16_t magic
 *   [2]      uint8_t  count   (number of populated entries)
 *   [3]      uint8_t  reserved
 *   [4..]    tiku_init_entry_t entries[TIKU_INIT_MAX_ENTRIES]
 *
 * Total: 4 + 8 * sizeof(tiku_init_entry_t) = 4 + 8*66 = 532 bytes
 */

/**
 * Byte offsets of each field within the config region.
 *
 *   OFF_MAGIC   (0) -- uint16_t magic word (OFF_COUNT follows at 2,
 *                      so the count byte is naturally past the word).
 *   OFF_COUNT   (2) -- uint8_t populated-entry count; the reserved
 *                      pad byte sits at OFF_COUNT + 1.
 *   OFF_ENTRIES (4) -- start of the tiku_init_entry_t array.
 *
 * These must stay consistent with the layout diagram above; the
 * helpers (init_read_magic / init_read_count / init_entry_ptr) index
 * the region through exactly these constants.
 */
#define OFF_MAGIC    0
#define OFF_COUNT    2
#define OFF_ENTRIES  4

/**
 * Total bytes the layout consumes: the 4-byte header plus the full
 * fixed array of entry slots.
 *
 * Deliberately written as a live sizeof() expression (not a hand-
 * computed literal) so it tracks any change to TIKU_INIT_MAX_ENTRIES
 * or tiku_init_entry_t automatically and feeds the static assert
 * below.
 */
#define TIKU_INIT_REGION_BYTES_NEEDED \
    (OFF_ENTRIES + (TIKU_INIT_MAX_ENTRIES * sizeof(tiku_init_entry_t)))

/*
 * Compile-time guard: the device's CONFIG region must be at least as
 * large as the init table needs.
 *
 * init_first_boot() zeroes every entry slot by writing through
 * init_entry_ptr(), which addresses cfg_base + OFF_ENTRIES + idx *
 * sizeof(entry).  If the device's TIKU_DEVICE_FRAM_CONFIG_SIZE is
 * smaller than TIKU_INIT_REGION_BYTES_NEEDED, those writes would run
 * past the region and clobber whatever NVM follows it — and because
 * the prime happens on first boot with the MPU unlocked, the
 * corruption would be silent.  Catching it here turns a runtime
 * memory-stomp into a build error.  The assert is gated on the macro
 * being defined so ports that size the CONFIG region differently (or
 * not via this symbol) still compile; the default sizes are 512B
 * (fr2433) up to 2048B (fr6989) against a 532B requirement at
 * TIKU_INIT_MAX_ENTRIES == 8.
 */
#if defined(TIKU_DEVICE_FRAM_CONFIG_SIZE)
_Static_assert(TIKU_DEVICE_FRAM_CONFIG_SIZE >= TIKU_INIT_REGION_BYTES_NEEDED,
    "TIKU_DEVICE_FRAM_CONFIG_SIZE is too small for the init table; "
    "init_first_boot will overflow into adjacent memory. "
    "Bump the device's TIKU_DEVICE_FRAM_CONFIG_SIZE to >= "
    "(4 + TIKU_INIT_MAX_ENTRIES * sizeof(tiku_init_entry_t)).");
#endif

/*---------------------------------------------------------------------------*/
/* INTERNAL STATE                                                            */
/*---------------------------------------------------------------------------*/

/**
 * Cached base address of the NVM config region.
 *
 * Resolved once by tiku_init_load() via tiku_nvm_region_get() and
 * reused by every helper and public entry point as the anchor for
 * the OFF_* offsets.  NULL until tiku_init_load() succeeds; every
 * public function guards on it, so calls made before load() (or when
 * the region is absent) become safe no-ops rather than NULL
 * dereferences.  Points into NVM, not SRAM — direct stores would
 * fault under the MPU, which is why all writes go through the unlock
 * helpers.
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

/**
 * @brief Write arbitrary bytes to NVM (caller must hold MPU unlocked).
 *
 * Thin wrapper around tiku_mem_arch_nvm_write() with casts for
 * convenience.
 *
 * @param fram_ptr  Destination address in NVM.
 * @param sram_ptr  Source address in SRAM.
 * @param len       Number of bytes to write.
 */
#define INIT_NVM_WRITE(fram_ptr, sram_ptr, len) \
    tiku_mem_arch_nvm_write((uint8_t *)(fram_ptr), \
                            (const uint8_t *)(sram_ptr), (len))

/**
 * @brief Compare two NUL-terminated strings for equality.
 *
 * Simple byte-by-byte comparison used for entry name matching.
 * Returns 1 if the strings are identical, 0 otherwise.
 *
 * @param a  First string.
 * @param b  Second string.
 * @return   1 if equal, 0 if different.
 */
static uint8_t
init_name_match(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == *b);
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
 * Called when init_read_magic() does not match TIKU_INIT_MAGIC,
 * indicating uninitialised NVM (fresh flash or corrupted region).
 * Zeros the count, reserved byte, and all entry slots, then writes
 * the magic word last as a commit marker so a power loss during
 * initialisation leaves the region detectable as uninitialised.
 */
static void
init_first_boot(void)
{
    uint16_t saved;
    uint16_t magic = TIKU_INIT_MAGIC;
    uint8_t  zero  = 0;
    uint8_t  zbuf[sizeof(tiku_init_entry_t)];
    uint8_t  i;

    memset(zbuf, 0, sizeof(zbuf));

    saved = tiku_mpu_unlock_nvm();

    /* Zero count */
    INIT_NVM_WRITE(cfg_base + OFF_COUNT, &zero, 1);

    /* Zero reserved byte */
    INIT_NVM_WRITE(cfg_base + OFF_COUNT + 1, &zero, 1);

    /* Zero all entry slots */
    for (i = 0; i < TIKU_INIT_MAX_ENTRIES; i++) {
        INIT_NVM_WRITE(init_entry_ptr(i), zbuf, sizeof(zbuf));
    }

    /* Write magic last — acts as commit marker */
    INIT_NVM_WRITE(cfg_base + OFF_MAGIC, &magic, sizeof(magic));

    tiku_mpu_lock_nvm(saved);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Resolve the config region and validate (or prime) the table.
 *
 * Looks up the CONFIG NVM region, caches its base in cfg_base, and
 * checks the magic word.  On a magic mismatch — blank or corrupted
 * NVM — it calls init_first_boot() to zero the table and stamp the
 * magic.  If the region is not allocated on this build the function
 * returns with cfg_base left NULL, which disables every other entry
 * point gracefully.
 *
 * Must be called once during boot after tiku_nvm_map_init() and
 * before tiku_init_run_all() (see main.c).  The only NVM write it can
 * trigger is the first-boot prime, which manages its own MPU unlock
 * window internally.
 */
void
tiku_init_load(void)
{
    const tiku_nvm_region_t *r;

    r = tiku_nvm_region_get(TIKU_NVM_REGION_CONFIG);
    if (r == (const tiku_nvm_region_t *)0) {
        return;
    }

    cfg_base = r->base;

    if (init_read_magic() != TIKU_INIT_MAGIC) {
        init_first_boot();
    }
}

/**
 * @brief Execute every enabled entry, ordered by sequence number.
 *
 * The boot replay step.  Builds an index array sorted by each entry's
 * seq field with an insertion sort (at most TIKU_INIT_MAX_ENTRIES
 * elements, so the O(n^2) cost is trivial and avoids pulling in a
 * library sort), then walks it in order.  Entries that are disabled
 * or carry an empty command are skipped.
 *
 * Each surviving command is copied from NVM into an SRAM scratch
 * buffer before dispatch because tiku_shell_parser_execute() tokenises
 * its argument in place (inserting NUL bytes at spaces) — it must not
 * write into the read-only NVM image, and a private copy also keeps
 * the persisted command text intact.  No NVM writes occur here; the
 * side effects are entirely whatever the dispatched shell commands do,
 * plus a "[init]" log line per executed entry.
 *
 * Reads served from cfg_base; returns 0 immediately if the table was
 * never loaded or is empty.
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

    if (cfg_base == (uint8_t *)0) {
        return 0;
    }

    count = init_read_count();
    if (count == 0) {
        return 0;
    }

    /* Build index sorted by seq (simple insertion sort — max 8 entries) */
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
 * @brief Add a new init entry, or replace an existing one by name.
 *
 * Assembles the entry in an SRAM stack struct first (zeroed, then
 * seq/enabled/name/cmd filled with bounded strncpy), so only one
 * fully-formed record is ever written to NVM.  If an entry with the
 * same name already exists it is overwritten in place — a single
 * entry-sized NVM write, no count change.  Otherwise the record is
 * appended at slot `count` and the count byte is bumped afterwards;
 * writing the entry before the count means a power loss between the
 * two writes leaves a stale slot that the (unincremented) count still
 * hides, never a counted-but-empty entry.
 *
 * The new entry is created enabled (entry.enabled = 1).  Each NVM
 * mutation is bracketed by tiku_mpu_unlock_nvm() / tiku_mpu_lock_nvm().
 *
 * @param seq   Boot sequence number; lower runs earlier.
 * @param name  Entry name, truncated to TIKU_INIT_NAME_SIZE-1 chars.
 * @param cmd   Shell command, truncated to TIKU_INIT_CMD_SIZE-1 chars.
 * @return 0 on success (added or replaced); -1 if the table was never
 *         loaded, an argument is NULL, or the table is full on add.
 */
int8_t
tiku_init_add(uint8_t seq, const char *name, const char *cmd)
{
    int8_t idx;
    uint8_t count;
    uint16_t saved;
    tiku_init_entry_t entry;

    if (cfg_base == (uint8_t *)0 || name == (const char *)0 ||
        cmd == (const char *)0) {
        return -1;
    }

    /* Prepare entry in SRAM */
    memset(&entry, 0, sizeof(entry));
    entry.seq = seq;
    entry.enabled = 1;
    strncpy(entry.name, name, TIKU_INIT_NAME_SIZE - 1);
    strncpy(entry.cmd, cmd, TIKU_INIT_CMD_SIZE - 1);

    idx = init_find(name);
    count = init_read_count();

    if (idx >= 0) {
        /* Replace existing entry */
        saved = tiku_mpu_unlock_nvm();
        INIT_NVM_WRITE(init_entry_ptr((uint8_t)idx), &entry, sizeof(entry));
        tiku_mpu_lock_nvm(saved);
        return 0;
    }

    /* Add new entry */
    if (count >= TIKU_INIT_MAX_ENTRIES) {
        return -1;
    }

    saved = tiku_mpu_unlock_nvm();
    INIT_NVM_WRITE(init_entry_ptr(count), &entry, sizeof(entry));
    count++;
    INIT_NVM_WRITE(cfg_base + OFF_COUNT, &count, 1);
    tiku_mpu_lock_nvm(saved);

    return 0;
}

/**
 * @brief Remove an init entry by name.
 *
 * Compacts the table with a swap-with-last: the final populated slot
 * is copied over the removed slot (unless the removed slot was
 * already last), the now-duplicate tail slot is zeroed, and the count
 * is decremented.  Entry order in NVM is irrelevant because
 * tiku_init_run_all() re-sorts by seq on every boot, so this O(1)
 * move is preferred over shifting the whole array.
 *
 * All three writes (the move, the tail zero, the count) happen inside
 * a single tiku_mpu_unlock_nvm() / tiku_mpu_lock_nvm() window.
 *
 * @param name  Name of the entry to remove (NUL-terminated).
 * @return 0 on success; -1 if the table was never loaded, @p name is
 *         NULL, or no entry matches.
 */
int8_t
tiku_init_remove(const char *name)
{
    int8_t idx;
    uint8_t count;
    uint16_t saved;

    if (cfg_base == (uint8_t *)0 || name == (const char *)0) {
        return -1;
    }

    idx = init_find(name);
    if (idx < 0) {
        return -1;
    }

    count = init_read_count();

    saved = tiku_mpu_unlock_nvm();

    /* Move last entry into the removed slot (order doesn't matter — seq sorts) */
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

    tiku_mpu_lock_nvm(saved);

    return 0;
}

/**
 * @brief Enable or disable an init entry by name.
 *
 * Flips just the entry's `enabled` flag in place: it normalises @p en
 * to 0/1 and writes that single byte to the address of the entry's
 * enabled field, leaving seq/name/cmd untouched.  A disabled entry
 * stays in the table (and in the count) but is skipped by
 * tiku_init_run_all().
 *
 * The one-byte write is bracketed by tiku_mpu_unlock_nvm() /
 * tiku_mpu_lock_nvm().
 *
 * @param name  Name of the entry to toggle (NUL-terminated).
 * @param en    Non-zero to enable, zero to disable.
 * @return 0 on success; -1 if the table was never loaded, @p name is
 *         NULL, or no entry matches.
 */
int8_t
tiku_init_enable(const char *name, uint8_t en)
{
    int8_t idx;
    uint16_t saved;
    uint8_t val;

    if (cfg_base == (uint8_t *)0 || name == (const char *)0) {
        return -1;
    }

    idx = init_find(name);
    if (idx < 0) {
        return -1;
    }

    val = en ? 1 : 0;

    saved = tiku_mpu_unlock_nvm();
    INIT_NVM_WRITE(&(init_entry_ptr((uint8_t)idx)->enabled), &val, 1);
    tiku_mpu_lock_nvm(saved);

    return 0;
}

/**
 * @brief Return the number of entries currently in the table.
 *
 * Counts both enabled and disabled entries (the on-NVM count byte
 * tracks populated slots, not just active ones).  Read-only, no MPU
 * interaction.
 *
 * @return Entry count, or 0 if the table was never loaded.
 */
uint8_t
tiku_init_count(void)
{
    if (cfg_base == (uint8_t *)0) {
        return 0;
    }
    return init_read_count();
}

/**
 * @brief Get a read-only pointer to the idx-th entry.
 *
 * Bounds-checks idx against the live count and returns a pointer
 * straight into the NVM config region — no copy.  Callers may read
 * the fields but must not write through the pointer (NVM is MPU-
 * protected); use tiku_init_add() / _remove() / _enable() to mutate.
 * Used by the "init" shell command to list the table.
 *
 * @param idx  Zero-based index, valid range 0 .. tiku_init_count()-1.
 * @return Pointer to the entry, or NULL if the table was never loaded
 *         or @p idx is out of range.
 */
const tiku_init_entry_t *
tiku_init_get(uint8_t idx)
{
    if (cfg_base == (uint8_t *)0 || idx >= init_read_count()) {
        return (const tiku_init_entry_t *)0;
    }
    return init_entry_ptr(idx);
}
