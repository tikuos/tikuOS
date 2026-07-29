/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_boot.h - /sys/boot VFS nodes and boot bookkeeping.
 *
 * Owns the boot-related persistent state: the boot counter, the lifetime-uptime
 * accumulator and the reset-cause snapshot taken at init.  Also exports three
 * top-level /sys read handlers so the /sys assembly can reference them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_BOOT_H_
#define TIKU_VFS_TREE_BOOT_H_

#include <stdint.h>
#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/boot.
 *
 * Must equal the number of initialisers in
 * tiku_vfs_tree_boot_children — bump it when adding a node there
 * (a _Static_assert in the .c catches a forgotten update).
 */
#define TIKU_VFS_TREE_BOOT_NCHILD  7

/**
 * @brief /sys/boot children: reason, count, stage, rstiv, clock/,
 *        mpu/.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_boot_children[];

/**
 * @brief Capture the reset cause and bump the FRAM boot counter.
 *
 * Validates this module's persist cells (boot counter, lifetime accumulator),
 * increments the counter and snapshots the accumulator.  Other modules'
 * persistent state is gated by its own cells and does not depend on this call.
 *
 * @note Must be the FIRST module init tiku_vfs_tree_init() calls: reading
 *       SYSRSTIV pops the highest pending cause, so an earlier read would
 *       consume the value snapshotted for /sys/boot/reason and /sys/boot/rstiv.
 */
void tiku_vfs_tree_boot_init(void);

/**
 * @brief Read handler for /sys/boot_count (also /sys/boot/count).
 *
 * Exported because the node appears in the /sys table owned by
 * tiku_vfs_tree_sys.c.  Renders the SRAM mirror of the FRAM boot
 * counter as a decimal line.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
int tiku_vfs_tree_boot_count_read(char *buf, size_t max);

/**
 * @brief Read handler for /sys/last_reset.
 *
 * Renders the coarse-bucketed reset cause ("watchdog\n", "power\n",
 * "reboot\n" or "other\n") — the script-friendly companion to the
 * detailed /sys/boot/reason.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
int tiku_vfs_tree_boot_last_reset_read(char *buf, size_t max);

/**
 * @brief Read handler for /sys/cold_boots.
 *
 * Renders the lifetime uptime (seconds accumulated across every
 * boot) as a decimal line, lazily persisting the fresh value to
 * FRAM as a side effect.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
int tiku_vfs_tree_boot_cold_boots_read(char *buf, size_t max);

#endif /* TIKU_VFS_TREE_BOOT_H_ */
