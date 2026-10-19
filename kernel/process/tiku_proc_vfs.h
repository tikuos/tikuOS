/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_proc_vfs.h - VFS /proc subtree registration.
 *
 * /proc shows the process registry, the event queue, the process catalog and
 * optional Wi-Fi and worker-thread status.  tiku_vfs_tree_init() copies the
 * node tiku_proc_vfs_get() returns into the root.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PROC_VFS_H_
#define TIKU_PROC_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Build the /proc VFS directory node from the registry and catalog.
 *
 * Each call rebuilds the node and its children in place, in static storage.
 * tiku_vfs_tree_init() copies the node, so the mounted /proc has a directory
 * for each process registered at tree assembly and for no later one.
 *
 * @return Pointer to the "proc" VFS directory node
 * @note Opens its own NVM unlock window (the tables are MPU-protected FRAM on
 *       MSP430) and restores the previous protection state.
 */
const tiku_vfs_node_t *tiku_proc_vfs_get(void);

/**
 * @brief Number of children a rebuild of /proc would give it now.
 *
 * Computed from the registry without a rebuild; the mounted /proc keeps the
 * count of tree assembly.
 *
 * @return The fixed entries (count, queue, catalog, and wifi and threads when
 *         built) plus one per registered process
 */
uint8_t tiku_proc_vfs_child_count(void);

#endif /* TIKU_PROC_VFS_H_ */
