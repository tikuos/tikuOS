/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_inittab.h - /sys/init VFS nodes (init-table mirror).
 *
 * Named after the classic Unix file rather than "init", which would collide with
 * the root assembly entry point.  The declarations are unconditional, so a build
 * with the table compiled out fails at link time -- loudly, by design.
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
