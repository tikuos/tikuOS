/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_vfs.h - Native media files and cached volume status.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_VFS_H_
#define TIKU_MEDIA_VFS_H_
#include "tiku_media_store.h"
#include <kernel/vfs/tiku_vfs.h>
/** @brief Attach a caller-owned service; NULL detaches it. No hardware is accessed. */
int tiku_media_vfs_attach(tiku_media_store_t *);
/** @brief Return the /media provider, with read-only legacy VFS operations. */
const tiku_vfs_node_t *tiku_media_vfs_files(void);
/** @brief Return cached /sys/media status without a block transaction. */
const tiku_vfs_node_t *tiku_media_vfs_status(void);
#endif
