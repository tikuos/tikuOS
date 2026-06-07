/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_inittab.h - /sys/init VFS nodes (init-table mirror)
 *
 * Named "inittab" (after the classic Unix file) rather than "init"
 * to avoid colliding with tiku_vfs_tree_init(), the root assembly
 * entry point.
 *
 * The table is only compiled when TIKU_INIT_ENABLE is set; the
 * declarations below are intentionally unconditional so consumers
 * guard usage with the same flag instead of needing this header to
 * know the build configuration.  Referencing the array in a build
 * where it is compiled out fails at link time — loudly, by design.
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

#ifndef TIKU_VFS_TREE_INITTAB_H_
#define TIKU_VFS_TREE_INITTAB_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/init: the "count" file plus one
 *        directory per init-table slot (TIKU_INIT_MAX_ENTRIES = 8).
 *
 * A _Static_assert in the .c keeps this in sync with the table.
 */
#define TIKU_VFS_TREE_INITTAB_NCHILD  9

/**
 * @brief /sys/init children: count, 0/..7/ (each slot directory
 *        holds seq, name, cmd, enable).
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c,
 * guarded there by #if TIKU_INIT_ENABLE.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_inittab_children[];

#endif /* TIKU_VFS_TREE_INITTAB_H_ */
