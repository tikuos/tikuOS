/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_coproc.h - /sys/coproc VFS nodes.
 *
 * Linkage contract for the coproc subtree: the children table is exported
 * with a compile-time entry count so the /sys assembly can embed it, and a
 * _Static_assert beside the table keeps the macro and the array in step.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_COPROC_H_
#define TIKU_VFS_TREE_COPROC_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/coproc.
 *
 * Must equal the number of initialisers in tiku_vfs_tree_coproc_children --
 * bump it when adding a node there (a _Static_assert in the .c catches a
 * forgotten update).
 */
#define TIKU_VFS_TREE_COPROC_NCHILD  5

/**
 * @brief /sys/coproc children: state, heartbeat, image, run, echo.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_coproc_children[];

#endif /* TIKU_VFS_TREE_COPROC_H_ */
