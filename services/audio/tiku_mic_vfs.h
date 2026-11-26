/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mic_vfs.h - The microphone's nodes, for boot to mount: mic under
 * /dev and audio/mic under /sys.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MIC_VFS_H_
#define TIKU_MIC_VFS_H_
#include <kernel/vfs/tiku_vfs.h>

/** @brief /dev/mic: each POSIX read moves the oldest waiting samples out. */
const tiku_vfs_node_t *tiku_mic_vfs_dev(void);

/** @brief /sys/audio/mic: enable and gain (capability HW), state, rate,
 *         buffered, captured, dropped, faults and peak. */
const tiku_vfs_node_t *tiku_mic_vfs_sys(void);

#endif
