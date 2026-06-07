/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_data.c - /data VFS nodes (user-data / persisted state)
 *
 * Bridges the Tiku BASIC program store to /data/basic so programs
 * can be saved and loaded through the VFS: `read /data/basic`
 * lists the stored program, writing replaces it.  The heavy
 * lifting (tokenising, FRAM arena management) lives in
 * kernel/shell/basic/; this module is only signature glue.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_data.h"
#include "tiku.h"

#if defined(TIKU_SHELL_ENABLE) && TIKU_SHELL_CMD_BASIC

#include "kernel/shell/basic/tiku_basic.h"

/*---------------------------------------------------------------------------*/
/* /data — user-data / persisted-state file nodes                            */
/*---------------------------------------------------------------------------*/

/*
 * Wrap the BASIC bridge in tiku_vfs handler signatures (size_t vs
 * unsigned int — same type on every TikuOS target but spelled
 * differently in the two headers).
 */

/**
 * @brief Read handler for /data/basic.
 *
 * Renders the stored BASIC program listing into @p buf via
 * tiku_basic_vfs_read().  An empty store renders nothing (returns
 * 0 bytes).
 *
 * @param buf  Output buffer for the program text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
data_basic_read(char *buf, size_t max)
{
    return tiku_basic_vfs_read(buf, (unsigned int)max);
}

/**
 * @brief Write handler for /data/basic.
 *
 * Feeds program text to tiku_basic_vfs_write(), which tokenises
 * and stores it in the interpreter's FRAM-backed program arena
 * (replacing the current program).
 *
 * @param buf  Program text to store
 * @param len  Text length in bytes
 * @return 0 on success, -1 on parse/store error
 */
static int
data_basic_write(const char *buf, size_t len)
{
    return tiku_basic_vfs_write(buf, (unsigned int)len);
}

/*---------------------------------------------------------------------------*/
/* NODE TABLES                                                               */
/*---------------------------------------------------------------------------*/

/** /data directory table — currently just the BASIC bridge */
static const tiku_vfs_node_t data_children[] = {
    { "basic", TIKU_VFS_FILE, data_basic_read, data_basic_write, NULL, 0 },
};

/**
 * The /data directory node itself, fully formed with its name so
 * the root assembly can copy it by value (getter pattern — see
 * tiku_vfs_tree_data_get()).
 */
static const tiku_vfs_node_t data_node = {
    "data", TIKU_VFS_DIR, NULL, NULL, data_children,
    sizeof(data_children) / sizeof(data_children[0])
};

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Get the fully-formed /data directory node.
 *
 * See the header for the copy-by-value contract with the root
 * assembly.
 *
 * @return Pointer to the static /data directory node
 */
const tiku_vfs_node_t *
tiku_vfs_tree_data_get(void)
{
    return &data_node;
}

#endif /* TIKU_SHELL_ENABLE && TIKU_SHELL_CMD_BASIC */
