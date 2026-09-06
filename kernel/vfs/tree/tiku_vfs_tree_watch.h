/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_watch.h - /sys/watch and /sys/vfs VFS nodes.
 *
 * Two read-only subtrees exposing the VFS core's own state through the VFS it
 * implements: watch-table contents for leak debugging, and tree statistics.
 * Same linkage contract as the sibling modules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_WATCH_H_
#define TIKU_VFS_TREE_WATCH_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/watch.
 *
 * Two summary counters (used, free) plus one node per watch slot,
 * so it tracks TIKU_VFS_WATCH_MAX automatically.  A _Static_assert
 * in the .c catches the per-slot table getting out of step.
 */
#define TIKU_VFS_TREE_WATCH_NCHILD  (2 + TIKU_VFS_WATCH_MAX)

/**
 * @brief /sys/watch children: used, free, and slots 0..MAX-1.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_watch_children[];

/**
 * @brief Entry count of /sys/vfs.
 *
 * Must equal the number of initialisers in
 * tiku_vfs_tree_vfs_children — bump it when adding a node there.
 */
#define TIKU_VFS_TREE_VFS_NCHILD  8

/**
 * @brief /sys/vfs children: nodes, depth, cache/.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_vfs_children[];

#endif /* TIKU_VFS_TREE_WATCH_H_ */
