/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_radio154.h - /sys/radio/154 VFS nodes.
 *
 * The children table is exported with a compile-time entry count so the
 * /sys/radio table can embed it; a _Static_assert beside the table keeps the
 * macro and the array in step.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_RADIO154_H_
#define TIKU_VFS_TREE_RADIO154_H_

#include <kernel/vfs/tiku_vfs.h>

#if (TIKU_HAS_154_LISTEN + 0)
/**
 * @brief Entry count of /sys/radio/154.
 *
 * Must equal the number of initialisers in tiku_vfs_tree_radio154_children
 * (a _Static_assert in the .c catches a forgotten update).
 */
#define TIKU_VFS_TREE_RADIO154_NCHILD  7

/**
 * @brief /sys/radio/154 children: channel, listen, tx, rx, rssi, held, ed.
 *
 * Referenced by the /sys/radio table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_radio154_children[];

/** @brief Point the listen's notify hook at /sys/radio/154/rx. */
void tiku_vfs_tree_radio154_init(void);
#endif /* TIKU_HAS_154_LISTEN */

#endif /* TIKU_VFS_TREE_RADIO154_H_ */
