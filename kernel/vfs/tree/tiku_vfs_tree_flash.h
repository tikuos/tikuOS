/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_flash.h - /sys/flash VFS nodes (Apollo510 EVB external NOR).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_FLASH_H_
#define TIKU_VFS_TREE_FLASH_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/flash.
 *
 * Must equal the number of initialisers in tiku_vfs_tree_flash_children
 * (a _Static_assert in the .c catches a forgotten update).
 */
#define TIKU_VFS_TREE_FLASH_NCHILD  6

/** @brief /sys/flash children: state, id, hz, size, mode, erases. */
extern const tiku_vfs_node_t tiku_vfs_tree_flash_children[];

#endif /* TIKU_VFS_TREE_FLASH_H_ */
