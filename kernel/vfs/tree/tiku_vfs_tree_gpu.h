/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_gpu.h - /sys/gpu VFS nodes (Apollo510 2.5D GPU)
 *
 * Linkage contract for the gpu subtree: children table + entry count
 * macro, consumed by the /sys assembly in tiku_vfs_tree_sys.c. Present
 * only when the from-scratch GPU driver is compiled (TIKU_DRV_GPU_ENABLE);
 * the /sys entry is gated on the same flag so the GPU-off image is
 * byte-identical.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_GPU_H_
#define TIKU_VFS_TREE_GPU_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/gpu.
 *
 * Must equal the number of initialisers in tiku_vfs_tree_gpu_children
 * (a _Static_assert in the .c catches a forgotten update).
 */
#define TIKU_VFS_TREE_GPU_NCHILD  4

/**
 * @brief /sys/gpu children: power, id, status, irqs.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_gpu_children[];

#endif /* TIKU_VFS_TREE_GPU_H_ */
