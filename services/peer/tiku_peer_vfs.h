/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_vfs.h - the board's peers under /sys/peer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PEER_VFS_H_
#define TIKU_PEER_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief The peer directory, for mounting under /sys: a directory per peer
 *        with state, role, carrier and restarts files.
 *
 * Built on the first call, from tiku_peer_table.  Defined only with
 * TIKU_PEER_ENABLE.
 */
const tiku_vfs_node_t *tiku_peer_vfs_get(void);

#endif /* TIKU_PEER_VFS_H_ */
