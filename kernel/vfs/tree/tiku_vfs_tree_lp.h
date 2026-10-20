/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_lp.h - /sys/lp, the ESP32-P4 LP agent's VFS nodes.
 *
 * Exports the /sys/lp children table and its entry count for the /sys
 * assembly; a _Static_assert beside the table checks the count.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_LP_H_
#define TIKU_VFS_TREE_LP_H_

#include <kernel/vfs/tiku_vfs.h>

/** @brief Entry count of /sys/lp. */
#define TIKU_VFS_TREE_LP_NCHILD  3

/** @brief /sys/lp children: jobs, samples, wake. */
extern const tiku_vfs_node_t tiku_vfs_tree_lp_children[];

/**
 * @brief Take the LP agent over for this boot (tiku_lpagent_boot()).
 * @note Called once from tiku_vfs_tree_sys_init(), after the flash mirror
 *       is restored.
 */
void tiku_vfs_tree_lp_init(void);

#endif /* TIKU_VFS_TREE_LP_H_ */
