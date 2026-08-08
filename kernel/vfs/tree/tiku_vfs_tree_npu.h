/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_npu.h - /sys/npu, the accelerator as named paths.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_NPU_H_
#define TIKU_VFS_TREE_NPU_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/npu.
 *
 * Must equal the number of initialisers in tiku_vfs_tree_npu_children; a
 * _Static_assert in the .c catches a forgotten update.
 */
#define TIKU_VFS_TREE_NPU_NCHILD  5

extern const tiku_vfs_node_t tiku_vfs_tree_npu_children[];

#endif /* TIKU_VFS_TREE_NPU_H_ */
