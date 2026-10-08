/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree.h - system VFS tree.
 *
 * Builds and initialises the root tree with /sys, /dev, /proc and /data.  The
 * node handlers live in the per-subtree modules under kernel/vfs/tree/; this is
 * the only header the rest of the system needs to bring the whole tree up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_H_
#define TIKU_VFS_TREE_H_

#include "tiku_vfs.h"

#define TIKU_VFS_BOOT_NODES_MAX 4u
#define TIKU_VFS_DATA_CHILDREN_MAX 1u

/** @brief Optional subtree attached under "/" or "/sys" at boot. */
typedef struct {
    const char *parent;
    const tiku_vfs_node_t *node;
} tiku_vfs_boot_node_t;

/** @brief Provider nodes and child tables must outlive the published tree. */
typedef struct {
    const tiku_vfs_node_t *data_children;
    uint8_t data_child_count;
    const tiku_vfs_boot_node_t *nodes;
    uint8_t node_count;
} tiku_vfs_tree_config_t;

/**
 * @brief Assemble the core tree and optional providers; NULL means core only.
 * Subtree hardware initialization runs once. Later calls rebuild the namespace.
 * @return OK or a VFS error; on failure no root or mounts remain published.
 * @note Call after hardware and process initialization, with dispatch stopped.
 */
int tiku_vfs_tree_init(const tiku_vfs_tree_config_t *config);

#endif /* TIKU_VFS_TREE_H_ */
