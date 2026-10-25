/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_ipc.h - portable IPC endpoint observation under /sys/ipc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_VFS_TREE_IPC_H_
#define TIKU_VFS_TREE_IPC_H_
#include <kernel/vfs/tiku_vfs.h>
#include <kernel/ipc/tiku_ipc_config.h>
#if TIKU_IPC_ENABLE
/** @brief Build static boot-lifetime IPC nodes and install change notification. */
int tiku_vfs_tree_ipc_init(void);
/** @brief The initialized /sys/ipc subtree, or NULL before initialization. */
const tiku_vfs_node_t *tiku_vfs_tree_ipc_get(void);
#endif
#endif
