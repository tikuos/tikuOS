/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_psram.c - /sys/psram VFS nodes (Apollo510 external 64 MB
 *                         PSRAM on MSPI0 -- EVB U14).
 *
 *   /sys/psram/state  "down" / "up" / "asleep" -- the lifecycle ladder
 *   /sys/psram/hz     IO clock in Hz (0 when down)
 *   /sys/psram/size   device size in bytes (fixed once identified)
 *   /sys/psram/tap    shipped RXDQSDELAY tap, or "unscanned"
 *
 * All read-only and power-safe: nothing here touches the device -- state
 * comes from the driver's own bookkeeping, so `cat` while down cannot
 * fault.  Compiled only under TIKU_DRV_PSRAM_ENABLE.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_vfs_tree_psram.h"
#include "tiku.h"
#include <arch/ambiq/tiku_psram_arch.h>
#include <stdio.h>

/** @brief The lifecycle rung, from driver bookkeeping only. */
static int
psram_state_read(char *buf, size_t max)
{
    const char *st = !tiku_psram_powered() ? "down"
                   : tiku_psram_asleep()   ? "asleep"
                                           : "up";
    return snprintf(buf, max, "%s\n", st);
}

/** @brief Live IO clock (0 when the controller is down). */
static int
psram_hz_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", tiku_psram_clock_hz());
}

/** @brief Device size -- a constant of the part, not of its power state. */
static int
psram_size_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)TIKU_PSRAM_SIZE_BYTES);
}

/** @brief Shipped timing tap, or "unscanned" before the first scan. */
static int
psram_tap_read(char *buf, size_t max)
{
    unsigned t = tiku_psram_tap();
    if (t == 0xFFu) {
        return snprintf(buf, max, "unscanned\n");
    }
    return snprintf(buf, max, "%u\n", t);
}

const tiku_vfs_node_t tiku_vfs_tree_psram_children[] = {
    { "state", TIKU_VFS_FILE, psram_state_read, NULL, NULL, 0 },
    { "hz",    TIKU_VFS_FILE, psram_hz_read,    NULL, NULL, 0 },
    { "size",  TIKU_VFS_FILE, psram_size_read,  NULL, NULL, 0 },
    { "tap",   TIKU_VFS_FILE, psram_tap_read,   NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_psram_children) /
               sizeof(tiku_vfs_tree_psram_children[0])
               == TIKU_VFS_TREE_PSRAM_NCHILD,
               "TIKU_VFS_TREE_PSRAM_NCHILD out of sync");
