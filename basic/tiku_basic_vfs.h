/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_BASIC_VFS_H_
#define TIKU_BASIC_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/** @brief Saved-program node for the static children of /data. */
const tiku_vfs_node_t *tiku_basic_vfs_get(void);

#endif
