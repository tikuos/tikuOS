/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_power.h - /sys/power VFS nodes
 *
 * Linkage contract for the power subtree: children table + entry
 * count macro, consumed by the /sys assembly in
 * tiku_vfs_tree_sys.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_POWER_H_
#define TIKU_VFS_TREE_POWER_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/power.
 *
 * Must equal the number of initialisers in
 * tiku_vfs_tree_power_children — bump it when adding a node there
 * (a _Static_assert in the .c catches a forgotten update).
 */
#define TIKU_VFS_TREE_POWER_NCHILD  2

/**
 * @brief /sys/power children: mode, wake.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_power_children[];

#endif /* TIKU_VFS_TREE_POWER_H_ */
