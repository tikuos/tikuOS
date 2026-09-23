/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_layout.h - /sys/mem/layout VFS nodes.
 *
 * The memory layout service's knobs, applied map, pending request and boot
 * findings as files, and its stage, cancel and resume operations as writes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_LAYOUT_H_
#define TIKU_VFS_TREE_LAYOUT_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/mem/layout.
 *
 * Must equal the number of initialisers in tiku_vfs_tree_layout_children;
 * a _Static_assert in the .c catches a forgotten update.
 */
#define TIKU_VFS_TREE_LAYOUT_NCHILD  7

/**
 * @brief /sys/mem/layout children: caps, current, pending, status, stage,
 *        cancel, resume.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_layout_children[];

#endif /* TIKU_VFS_TREE_LAYOUT_H_ */
