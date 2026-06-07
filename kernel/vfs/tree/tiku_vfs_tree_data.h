/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_data.h - /data VFS nodes (user-data / persisted state)
 *
 * The /data directory holds user-facing persisted content (as
 * opposed to /sys system state and /dev hardware).  Currently its
 * only member is the BASIC program store, so the module compiles
 * away entirely unless the shell's BASIC interpreter is enabled.
 * The declaration below is unconditional so the root assembly can
 * guard the call with the same build flags instead of needing this
 * header to know the configuration.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_DATA_H_
#define TIKU_VFS_TREE_DATA_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Get the fully-formed /data directory node.
 *
 * Mirrors tiku_proc_vfs_get(): returns a pointer to a static,
 * fully-initialised DIR node named "data"; the root assembly
 * copies it by value into the mutable FRAM root-children array at
 * init time.  Only defined when TIKU_SHELL_ENABLE and
 * TIKU_SHELL_CMD_BASIC are both set — callers must guard with the
 * same condition.
 *
 * @return Pointer to the static /data directory node
 */
const tiku_vfs_node_t *tiku_vfs_tree_data_get(void);

#endif /* TIKU_VFS_TREE_DATA_H_ */
