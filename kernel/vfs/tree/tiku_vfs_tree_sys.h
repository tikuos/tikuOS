/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_sys.h - /sys subtree (files and assembly).
 *
 * Owns the top-level /sys files and the small static device/mem/cpu/sched
 * subtrees, and assembles the whole directory from the sibling modules.  The root
 * assembly sees only the two functions below; child tables stay private.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_SYS_H_
#define TIKU_VFS_TREE_SYS_H_

#include <stdint.h>
#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Get the fully-formed /sys directory node.
 *
 * Mirrors tiku_proc_vfs_get(): returns a pointer to a static,
 * fully-initialised DIR node named "sys" whose child count is
 * computed by sizeof inside this module — the root assembly copies
 * it by value into the mutable FRAM root-children array at init
 * time, so no count macro crosses this boundary.
 *
 * @return Pointer to the static /sys directory node
 */
const tiku_vfs_node_t *tiku_vfs_tree_sys_get(void);

/**
 * @brief Initialise /sys state (RTC epoch, device name default).
 *
 * Validates the persistent RTC epoch offset via tiku_rtc_init()
 * (idempotent, gated by its own persist cell) and validates/primes
 * the device-name cell to its "tiku" default.  Both carry their own
 * magic gates, so this no longer depends on the boot module's init
 * or any cross-module first-boot flag.
 */
void tiku_vfs_tree_sys_init(void);

#endif /* TIKU_VFS_TREE_SYS_H_ */
