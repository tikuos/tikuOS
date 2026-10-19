/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_sys.h - /sys subtree (files and assembly).
 *
 * Owns the top-level /sys files and the small static device/mem/cpu/sched
 * subtrees, and assembles the whole directory from the sibling modules.  The
 * root assembly sees only the two functions below; child tables stay private.
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
 * Returns a static DIR node named "sys", its child count computed by sizeof
 * inside the module; the root assembly copies it by value into its
 * root-children array.
 *
 * @return Pointer to the static /sys directory node
 */
const tiku_vfs_node_t *tiku_vfs_tree_sys_get(void);

/**
 * @brief Initialise /sys state: RTC epoch, device name, configuration
 *        journal and the radio scan hook.
 *
 * Validates the RTC epoch offset via tiku_rtc_init() and validates or primes
 * the device-name cell to "tiku".  With TIKU_VFS_CONFIG_ENABLE it also opens
 * the configuration journal and applies its settings, where the port has one.
 *
 * @note The ports with journal banks are the #if in
 *       tiku_vfs_config_service.inl (MSP430, Nordic); elsewhere that step
 *       does nothing.
 */
void tiku_vfs_tree_sys_init(void);

#endif /* TIKU_VFS_TREE_SYS_H_ */
