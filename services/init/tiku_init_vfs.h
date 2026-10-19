/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_init_vfs.h - Init table subtree of the VFS
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_INIT_VFS_H_
#define TIKU_INIT_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Return the init directory node, for mounting under /sys.
 *
 * Getting it neither loads the table nor runs commands.  Defined only when
 * TIKU_INIT_ENABLE is set.
 */
const tiku_vfs_node_t *tiku_init_vfs_get(void);

#endif
