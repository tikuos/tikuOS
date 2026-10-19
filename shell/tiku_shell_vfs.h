/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_vfs.h - Read-only VFS views of shell rules and jobs
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_VFS_H_
#define TIKU_SHELL_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Return the read-only rules and jobs directories, each with count and
 *        list files, for mounting under /sys.
 *
 * Getting one changes neither table and starts no work.  Defined only when
 * TIKU_SHELL_ENABLE is set.
 */
const tiku_vfs_node_t *tiku_shell_vfs_rules(void);
const tiku_vfs_node_t *tiku_shell_vfs_jobs(void);

#endif
