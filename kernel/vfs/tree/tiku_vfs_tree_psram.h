/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_psram.h - /sys/psram VFS nodes (Apollo510 external PSRAM)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_PSRAM_H_
#define TIKU_VFS_TREE_PSRAM_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/psram.
 *
 * Must equal the number of initialisers in tiku_vfs_tree_psram_children
 * (a _Static_assert in the .c catches a forgotten update).
 */
#define TIKU_VFS_TREE_PSRAM_NCHILD  4

/** @brief /sys/psram children: state, hz, size, tap. */
extern const tiku_vfs_node_t tiku_vfs_tree_psram_children[];

#endif /* TIKU_VFS_TREE_PSRAM_H_ */
