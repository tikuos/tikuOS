/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sd_vfs.h - Read-only /sd files and /sys/sd operation diagnostics.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SD_VFS_H_
#define TIKU_SD_VFS_H_
#include <kernel/vfs/tiku_vfs.h>
/** @brief Static /sd provider; obtaining it does not access the card. */
const tiku_vfs_node_t *tiku_sd_vfs_files(void);
/** @brief Static /sys/sd file reporting the most recent VFS SD operation. */
const tiku_vfs_node_t *tiku_sd_vfs_status(void);
#endif
