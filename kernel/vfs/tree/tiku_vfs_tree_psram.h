/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_psram.h - /sys/psram VFS nodes.
 *
 * Linkage contract for the PSRAM subtree: the children table and its entry
 * count, consumed by the /sys assembly.  One port file defines the table:
 * tiku_vfs_tree_psram.c (Apollo510), _psram_c5.c or _psram_p4.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_PSRAM_H_
#define TIKU_VFS_TREE_PSRAM_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/psram.
 *
 * Must equal the number of initialisers in tiku_vfs_tree_psram_children
 * (a _Static_assert in the .c catches a forgotten update).
 */
#if defined(PLATFORM_ESP32C5)
#define TIKU_VFS_TREE_PSRAM_NCHILD  5
#else
#define TIKU_VFS_TREE_PSRAM_NCHILD  4
#endif

/**
 * @brief /sys/psram children: state, hz, size, tap on Apollo510; state,
 *        size, id, package_code, result on ESP32-C5; state, size, hz, stage
 *        on ESP32-P4.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_psram_children[];

#endif /* TIKU_VFS_TREE_PSRAM_H_ */
