/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_INIT_VFS_H_
#define TIKU_INIT_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/** @brief Init table subtree; getting it neither loads nor runs commands. */
const tiku_vfs_node_t *tiku_init_vfs_get(void);

#endif
