/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_psram.c - /sys/psram VFS nodes (Apollo510 external 64 MB PSRAM).
 *
 * State (writable: the lifecycle verbs up/down/sleep/wake), IO clock, device
 * size and the shipped RXDQSDELAY tap.  Reads come from driver bookkeeping
 * only, so a read while the device is down cannot fault.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_vfs_tree_psram.h"
#include "tiku.h"
#include <arch/ambiq/tiku_psram_arch.h>
#include <stdio.h>
#include <string.h>

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

/**
 * @brief Write handler for /sys/psram/state: the lifecycle verbs.
 *
 * "up" runs the full bring-up at 192 MHz (identity, scan, XIP, tier attach);
 * "down" refuses while tier allocations are live -- the no-dangling-pointer
 * contract, surfaced as a failed write; "sleep"/"wake" drive half-sleep.
 */
static int
psram_state_write(const char *buf, size_t len)
{
    char v[8];
    size_t n = 0;

    while (n < len && n < sizeof(v) - 1u &&
           buf[n] != '\n' && buf[n] != '\r' && buf[n] != '\0') {
        v[n] = buf[n];
        n++;
    }
    v[n] = '\0';

    if (strcmp(v, "up") == 0) {
        return (tiku_psram_up(TIKU_PSRAM_CLK_192MHZ, 1) == TIKU_PSRAM_OK)
               ? 0 : -1;
    }
    if (strcmp(v, "down") == 0) {
        return (tiku_psram_down(0) == TIKU_PSRAM_OK) ? 0 : -1;
    }
    if (strcmp(v, "sleep") == 0) {
        return (tiku_psram_halfsleep() == TIKU_PSRAM_OK) ? 0 : -1;
    }
    if (strcmp(v, "wake") == 0) {
        if (tiku_psram_wake() != TIKU_PSRAM_OK) {
            return -1;
        }
        (void)tiku_psram_xip_enable(1);   /* wake leaves XIP unmapped */
        return 0;
    }
    return -1;
}

const tiku_vfs_node_t tiku_vfs_tree_psram_children[] = {
    { "state", TIKU_VFS_FILE, psram_state_read, psram_state_write, NULL, 0,
      NULL, NULL, TIKU_VFS_CAP_SYS },
    { "hz",    TIKU_VFS_FILE, psram_hz_read,    NULL, NULL, 0 },
    { "size",  TIKU_VFS_FILE, psram_size_read,  NULL, NULL, 0 },
    { "tap",   TIKU_VFS_FILE, psram_tap_read,   NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_psram_children) /
               sizeof(tiku_vfs_tree_psram_children[0])
               == TIKU_VFS_TREE_PSRAM_NCHILD,
               "TIKU_VFS_TREE_PSRAM_NCHILD out of sync");
