/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_boot.h - /sys/boot VFS nodes and boot bookkeeping.
 *
 * Owns the boot counter and the lifetime-uptime accumulator (persist cells)
 * and the reset-cause snapshot taken at init.  Also exports three top-level
 * /sys read handlers for the /sys assembly.
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
 * @brief /sys/boot children: reason, count, stage, rstiv, hang, clock/,
 *        mpu/.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_boot_children[];

/**
 * @brief Capture the reset cause and bump the durable boot counter.
 *
 * Latches the reset cause and the hang culprit (tiku_hang_boot_init()),
 * primes an invalid boot-counter or lifetime cell to 0, increments the
 * counter and snapshots the lifetime.  Other modules' cells do not need it.
 *
 * @note Call before any other module init in tiku_vfs_tree_init(): on MSP430
 *       reading SYSRSTIV pops the highest pending cause, so an earlier read
 *       would consume the value /sys/boot/reason and /sys/boot/rstiv report.
 */
void tiku_vfs_tree_boot_init(void);

/**
 * @brief Read handler for /sys/boot_count (also /sys/boot/count).
 *
 * Renders the SRAM copy of the durable boot counter as a decimal line: the
 * boots since the cell was first primed, 1 on the first ("17\n" on the
 * seventeenth).  The /sys table in tiku_vfs_tree_sys.c references it.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
 */
int tiku_vfs_tree_boot_count_read(char *buf, size_t max);

/**
 * @brief Read handler for /sys/last_reset.
 *
 * Renders the reset cause latched by tiku_vfs_tree_boot_init() in one of four
 * buckets ("watchdog\n", "power\n", "reboot\n" or "other\n");
 * /sys/boot/reason gives the detailed name.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
 */
int tiku_vfs_tree_boot_last_reset_read(char *buf, size_t max);

/**
 * @brief Read handler for /sys/cold_boots: the lifetime uptime in seconds.
 *
 * Named cold_boots, it renders the seconds of uptime summed over every boot
 * since the cell was primed, not a boot count.  Each read saves the value to
 * its persist cell, so a power loss loses only the time since the last read.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style), or TIKU_VFS_EIO when
 *         the cell cannot be saved
 * @note On MSP430 the save clears and re-sets the cell's gate around two word
 *       stores, so a power cut inside it re-primes the lifetime to 0.
 */
int tiku_vfs_tree_boot_cold_boots_read(char *buf, size_t max);

#endif /* TIKU_VFS_TREE_BOOT_H_ */
