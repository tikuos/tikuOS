/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_flash.c - /sys/flash VFS nodes (Apollo510 EVB external NOR).
 *
 * State, identity, clock, size, bus mode and erase count, all read-only.
 * Values come from driver bookkeeping; no handler issues a bus command, so a
 * read while the part is down or unpowered cannot fault or wake it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_vfs_tree_flash.h"
#include "tiku.h"
#include <arch/ambiq/tiku_nor_arch.h>
#include <stdio.h>

/** @brief "up" or "down", from driver bookkeeping only. */
static int
flash_state_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", tiku_nor_powered() ? "up" : "down");
}

/**
 * @brief Cached JEDEC identity while the part is up, else "unread".
 *
 * Reads the cached identity, so no bus command is issued.  A part that is
 * down reads "unread" even after an earlier successful read.
 */
static int
flash_id_read(char *buf, size_t max)
{
    tiku_nor_id_t id;

    if (!tiku_nor_powered() || tiku_nor_id_cached(&id) != 0) {
        return snprintf(buf, max, "unread\n");
    }
    return snprintf(buf, max, "%02x %02x %02x\n", id.mfr, id.type,
                    id.capacity);
}

/** @brief Bus clock in Hz (0 when the controller is down). */
static int
flash_hz_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", tiku_nor_clock_hz());
}

/** @brief Device size in bytes, a constant reported in any power state. */
static int
flash_size_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)TIKU_NOR_SIZE_BYTES);
}

/** @brief Bus mode, "octal-ddr" or "serial"; "down" when unpowered. */
static int
flash_mode_read(char *buf, size_t max)
{
    if (!tiku_nor_powered()) { return snprintf(buf, max, "down\n"); }
    return snprintf(buf, max, "%s\n",
                    tiku_nor_is_octal() ? "octal-ddr" : "serial");
}

/** @brief Erases issued since boot; each counts against the endurance. */
static int
flash_erases_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)tiku_nor_erase_count());
}

const tiku_vfs_node_t tiku_vfs_tree_flash_children[] = {
    { "state",  TIKU_VFS_FILE, flash_state_read,  NULL, NULL, 0 },
    { "id",     TIKU_VFS_FILE, flash_id_read,     NULL, NULL, 0 },
    { "hz",     TIKU_VFS_FILE, flash_hz_read,     NULL, NULL, 0 },
    { "size",   TIKU_VFS_FILE, flash_size_read,   NULL, NULL, 0 },
    { "mode",   TIKU_VFS_FILE, flash_mode_read,   NULL, NULL, 0 },
    { "erases", TIKU_VFS_FILE, flash_erases_read, NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_flash_children) /
               sizeof(tiku_vfs_tree_flash_children[0])
               == TIKU_VFS_TREE_FLASH_NCHILD,
               "TIKU_VFS_TREE_FLASH_NCHILD out of sync");
