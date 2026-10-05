/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree.h - system VFS tree.
 *
 * Builds and initialises the root tree with /sys, /dev, /proc and /data.  The
 * node handlers live in the per-subtree modules under kernel/vfs/tree/; this is
 * the only header the rest of the system needs to bring the whole tree up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_H_
#define TIKU_VFS_TREE_H_

#include <stdint.h>

/**
 * @brief Build and register the system VFS tree.
 *
 * Runs the per-subtree module inits (boot counter and reset cause, LEDs, RTC
 * epoch, device name, configuration journal), assembles the top-level
 * directories into the durable root and registers it with tiku_vfs_init().
 *
 * @note Call once during boot, after hardware and process init.
 */
void tiku_vfs_tree_init(void);

#endif /* TIKU_VFS_TREE_H_ */
