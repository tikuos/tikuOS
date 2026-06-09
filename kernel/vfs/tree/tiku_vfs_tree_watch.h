/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_watch.h - /sys/watch and /sys/vfs VFS nodes
 *
 * The namespace observing itself: two small read-only subtrees that
 * expose the VFS core's private state through the same VFS it
 * implements.
 *
 *   /sys/watch   the watch-table occupancy and per-slot contents
 *                (subscription-leak / self-heal debugging)
 *   /sys/vfs     tree statistics (node count, depth)
 *
 * Linkage contract matches the other subtree modules: exported
 * children tables + compile-time entry counts, consumed by the /sys
 * assembly in tiku_vfs_tree_sys.c.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
#define TIKU_VFS_TREE_VFS_NCHILD  2

/**
 * @brief /sys/vfs children: nodes, depth.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_vfs_children[];

#endif /* TIKU_VFS_TREE_WATCH_H_ */
