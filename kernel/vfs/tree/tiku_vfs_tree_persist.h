/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_persist.h - /sys/persist VFS nodes.
 *
 * Observability for the persist-cell layer: cells validated, primed and moved
 * this boot, and the cell manifest.  Exports the children table and its entry
 * count for the /sys assembly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_PERSIST_H_
#define TIKU_VFS_TREE_PERSIST_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/persist.
 *
 * Must equal the number of initialisers in
 * tiku_vfs_tree_persist_children — bump it when adding a node there
 * (a _Static_assert in the .c catches a forgotten update).
 */
#define TIKU_VFS_TREE_PERSIST_NCHILD  4

/**
 * @brief /sys/persist children: cells, primed, moved, manifest.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_persist_children[];

#endif /* TIKU_VFS_TREE_PERSIST_H_ */
