/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_vfs.h - Saved-program node for the static children of /data
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BASIC_VFS_H_
#define TIKU_BASIC_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Return /data/basic, the saved BASIC program, as a static node.
 *
 * Returns a one-entry array for the static children of /data; the program's
 * storage is read only when the node is read.  Defined only when
 * TIKU_SHELL_ENABLE and TIKU_SHELL_CMD_BASIC are set.
 */
const tiku_vfs_node_t *tiku_basic_vfs_get(void);

#endif
