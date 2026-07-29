/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_proc_vfs.h - VFS /proc subtree registration.
 *
 * Public interface to the live runtime counterpart of the static /sys subtree.
 * The VFS root assembly calls tiku_proc_vfs_get() for the top-level directory and
 * _child_count() to size it.  See the .c file for the full subtree map.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PROC_VFS_H_
#define TIKU_PROC_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Get the /proc VFS directory node.
 *
 * Rebuilt from scratch on each call, so it reflects the current registry,
 * catalog and driver state.  The node and its children live in durable storage
 * and the pointer stays valid only until the next call overwrites it.
 *
 * @return Pointer to the "proc" VFS directory node
 */
const tiku_vfs_node_t *tiku_proc_vfs_get(void);

/**
 * @brief Get the number of child nodes under /proc.
 *
 * The count /proc would have right now: the fixed entries plus one directory
 * per registered process.  Computed from the live registry, so it tracks _get()
 * without forcing a rebuild.
 *
 * @return child_count of the /proc directory node
 */
uint8_t tiku_proc_vfs_child_count(void);

#endif /* TIKU_PROC_VFS_H_ */
