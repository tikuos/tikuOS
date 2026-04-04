/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_init.c - NVM-backed configurable boot (init system)
 *
 * The init table is stored inside a non-volatile memory (NVM) config
 * region obtained from the NVM region map.  A magic word detects
 * first-boot and auto-clears.  All NVM writes go through the
 * platform's memory-protection unlock / nvm_write / lock sequence.
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

/** Magic value to detect uninitialised NVM on first boot */
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

/** Offsets into the config region */
#define OFF_MAGIC    0
#define OFF_COUNT    2
#define OFF_ENTRIES  4

/*---------------------------------------------------------------------------*/
/* INTERNAL STATE                                                            */
/*---------------------------------------------------------------------------*/

/** Cached pointer to the config region base (set by tiku_init_load) */
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

uint8_t
tiku_init_count(void)
{
    if (cfg_base == (uint8_t *)0) {
        return 0;
    }
    return init_read_count();
}

const tiku_init_entry_t *
tiku_init_get(uint8_t idx)
{
    if (cfg_base == (uint8_t *)0 || idx >= init_read_count()) {
        return (const tiku_init_entry_t *)0;
    }
    return init_entry_ptr(idx);
}
