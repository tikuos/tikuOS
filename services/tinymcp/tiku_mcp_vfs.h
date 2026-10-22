/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_vfs.h - /sys/mcp: the server's state and counters as nodes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MCP_VFS_H_
#define TIKU_MCP_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/** @brief The /sys/mcp subtree, for the boot assembly to mount. */
const tiku_vfs_node_t *tiku_mcp_vfs_get(void);

#endif /* TIKU_MCP_VFS_H_ */
