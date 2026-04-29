/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs.h - Virtual Filesystem public API and types
 *
 * Exposes all device state (sensors, config, network, memory, processes)
 * as a tree of named paths.  No block storage, no inodes — just a static
 * tree of nodes with read/write handler functions.
 *
 * Unified access: CLI `cat /dev/temp0`, CoAP `GET /dev/temp0`, and
 * application code `tiku_vfs_read("/dev/temp0", ...)` all use the same
 * path and get the same result.
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

#ifndef TIKU_VFS_H_
#define TIKU_VFS_H_

#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* NODE TYPES                                                                */
/*---------------------------------------------------------------------------*/

/** @brief VFS node type */
typedef enum {
    TIKU_VFS_DIR,
    TIKU_VFS_FILE
} tiku_vfs_type_t;

/*---------------------------------------------------------------------------*/
/* HANDLER FUNCTION TYPES                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler: write human-readable value into buf
 * @return bytes written, or -1 on error
 */
typedef int (*tiku_vfs_read_fn)(char *buf, size_t max);

/**
 * @brief Write handler: receive string value
 * @return 0 on success, -1 on error
 */
typedef int (*tiku_vfs_write_fn)(const char *buf, size_t len);

/*---------------------------------------------------------------------------*/
/* VFS NODE                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief A node in the VFS tree */
typedef struct tiku_vfs_node {
    const char                  *name;        /**< Path component */
    tiku_vfs_type_t              type;         /**< DIR or FILE */
    tiku_vfs_read_fn             read;         /**< NULL if not readable */
    tiku_vfs_write_fn            write;        /**< NULL if not writable */
    const struct tiku_vfs_node  *children;     /**< For DIR: child array */
    uint8_t                      child_count;  /**< For DIR: child count */
} tiku_vfs_node_t;

/*---------------------------------------------------------------------------*/
/* LIST CALLBACK                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Callback for tiku_vfs_list(), called once per child
 */
typedef void (*tiku_vfs_list_fn)(const struct tiku_vfs_node *node,
                                  void *ctx);

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize VFS with root node
 * @param root  Root directory node of the tree
 */
void tiku_vfs_init(const tiku_vfs_node_t *root);

/**
 * @brief Resolve a path to its node
 * @param path  Absolute path (must start with "/")
 * @return Matching node, or NULL if not found
 */
const tiku_vfs_node_t *tiku_vfs_resolve(const char *path);

/**
 * @brief Read from a path
 * @param path  Absolute path to a FILE node
 * @param buf   Output buffer
 * @param max   Buffer capacity
 * @return Bytes written to buf, or -1 on error
 */
int tiku_vfs_read(const char *path, char *buf, size_t max);

/**
 * @brief Write to a path
 * @param path  Absolute path to a writable FILE node
 * @param data  Data to write
 * @param len   Data length
 * @return 0 on success, -1 on error
 */
int tiku_vfs_write(const char *path, const char *data, size_t len);

/**
 * @brief List directory contents
 * @param path      Absolute path to a DIR node
 * @param callback  Called once per child
 * @param ctx       User context passed to callback
 * @return 0 on success, -1 on error (not found or not a directory)
 */
int tiku_vfs_list(const char *path, tiku_vfs_list_fn callback, void *ctx);

#endif /* TIKU_VFS_H_ */
