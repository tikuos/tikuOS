/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_watchdog.h - /sys/watchdog VFS nodes
 *
 * Linkage contract for the watchdog subtree: the children table is
 * exported together with a compile-time entry count so the parent
 * (/sys assembly in tiku_vfs_tree_sys.c) can embed it in its own
 * static directory table.  A _Static_assert next to the table
 * definition guarantees the macro matches the array.
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

#ifndef TIKU_VFS_TREE_WATCHDOG_H_
#define TIKU_VFS_TREE_WATCHDOG_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/watchdog.
 *
 * Must equal the number of initialisers in
 * tiku_vfs_tree_watchdog_children — bump it when adding a node
 * there (a _Static_assert in the .c catches a forgotten update).
 */
#define TIKU_VFS_TREE_WATCHDOG_NCHILD  6

/**
 * @brief /sys/watchdog children: mode, clock, interval, kick,
 *        enabled, kicks.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_watchdog_children[];

#endif /* TIKU_VFS_TREE_WATCHDOG_H_ */
