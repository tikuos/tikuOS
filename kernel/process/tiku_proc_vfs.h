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
 * @brief Get the /proc VFS directory node
 *
 * Returns a pointer to the top-level "proc" directory node that can
 * be included in the VFS root tree. The node tree is rebuilt from
 * scratch each time this function is called so it reflects the
 * current process registry, catalog and driver state.
 *
 * The returned node and all its children live in the .persistent
 * (FRAM) section; the rebuild runs inside an NVM MPU-unlock window
 * (see the implementation).  The pointer stays valid until the next
 * call, which overwrites the same storage.
 *
 * @return Pointer to the "proc" VFS directory node
 */
const tiku_vfs_node_t *tiku_proc_vfs_get(void);

/**
 * @brief Get the number of child nodes under /proc
 *
 * Returns the count the /proc directory would have right now: the
 * fixed entries (count, queue, catalog, plus wifi when a wireless
 * driver is built) plus one sub-directory per registered process.
 * Computed directly from the live registry, so it tracks
 * tiku_proc_vfs_get() without forcing a tree rebuild.
 *
 * @return child_count of the /proc directory node
 */
uint8_t tiku_proc_vfs_child_count(void);

#endif /* TIKU_PROC_VFS_H_ */
