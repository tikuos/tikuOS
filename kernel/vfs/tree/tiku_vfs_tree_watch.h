/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_watch.h - /sys/watch and /sys/vfs VFS nodes.
 *
 * Two read-only subtrees: the watch table's slots, and the tree's node
 * count, depth, manifest, change ring and read-cache counters.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_WATCH_H_
#define TIKU_VFS_TREE_WATCH_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/watch.
 *
 * Two summary counters (used, free) and one node per watch slot.  The .c
 * asserts that its table has this many entries and that
 * TIKU_VFS_WATCH_MAX is 8.
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
 * Must equal the number of initialisers in tiku_vfs_tree_vfs_children; a
 * _Static_assert in the .c checks it.
 */
#define TIKU_VFS_TREE_VFS_NCHILD  8

/**
 * @brief /sys/vfs children: nodes, depth, manifest, manifest_rev, events,
 *        events_pending, events_dropped, cache/.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_vfs_children[];

#endif /* TIKU_VFS_TREE_WATCH_H_ */
