/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_emmc.h - /sys/emmc VFS nodes (Apollo510 SDIO0 + EVB U11)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_EMMC_H_
#define TIKU_VFS_TREE_EMMC_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/emmc.
 *
 * Must equal the number of initialisers in tiku_vfs_tree_emmc_children
 * (a _Static_assert in the .c catches a forgotten update).
 */
#define TIKU_VFS_TREE_EMMC_NCHILD  5

/** @brief /sys/emmc children: state, cid, size, hz, width. */
extern const tiku_vfs_node_t tiku_vfs_tree_emmc_children[];

#endif /* TIKU_VFS_TREE_EMMC_H_ */
