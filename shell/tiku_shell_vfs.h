/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_SHELL_VFS_H_
#define TIKU_SHELL_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/** @brief Read-only views of shell rules and jobs; getters start no work. */
const tiku_vfs_node_t *tiku_shell_vfs_rules(void);
const tiku_vfs_node_t *tiku_shell_vfs_jobs(void);

#endif
