/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs.c - Tree walker, path resolver, read/write dispatch
 *
 * The VFS core is intentionally minimal: it resolves slash-separated
 * paths against a static tree of nodes and dispatches to read/write
 * handler functions.  No malloc, no string copies, no inodes.
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

#include "tiku_vfs.h"

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static const tiku_vfs_node_t *vfs_root;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compare a path component against a node name
 *
 * Matches the characters in [comp, comp+len) against name.
 * Returns 1 if equal, 0 otherwise.
 */
static int comp_match(const char *comp, size_t len, const char *name)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (name[i] == '\0' || name[i] != comp[i]) {
            return 0;
        }
    }

    return name[len] == '\0';
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Register the root node of the VFS tree.
 *
 * All subsequent resolve/read/write calls walk from this root.
 * The tree is static (built at compile time); init just stores
 * the pointer.
 */
void tiku_vfs_init(const tiku_vfs_node_t *root)
{
    vfs_root = root;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Walk the VFS tree to resolve a slash-separated path.
 *
 * Splits the path into components, descends through directory
 * nodes by matching each component against child names (linear
 * scan per level).  Returns NULL on any mismatch, non-directory
 * intermediate, or NULL root.  Handles leading/trailing/duplicate
 * slashes gracefully.
 */
const tiku_vfs_node_t *tiku_vfs_resolve(const char *path)
{
    const tiku_vfs_node_t *node;
    const char *p;
    const char *comp;
    size_t comp_len;
    uint8_t i;
    int found;

    if (path == NULL || path[0] != '/' || vfs_root == NULL) {
        return NULL;
    }

    node = vfs_root;
    p = path + 1;  /* skip leading '/' */

    /* Root path: "/" or empty after slash */
    if (*p == '\0') {
        return node;
    }

    while (*p != '\0') {
        /* Skip consecutive slashes */
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;  /* trailing slash */
        }

        /* Extract component */
        comp = p;
        while (*p != '/' && *p != '\0') {
            p++;
        }
        comp_len = (size_t)(p - comp);

        /* Current node must be a directory to descend */
        if (node->type != TIKU_VFS_DIR || node->children == NULL) {
            return NULL;
        }

        /* Search children for matching name */
        found = 0;
        for (i = 0; i < node->child_count; i++) {
            if (comp_match(comp, comp_len, node->children[i].name)) {
                node = &node->children[i];
                found = 1;
                break;
            }
        }

        if (!found) {
            return NULL;
        }
    }

    return node;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Resolve a path and invoke the file's read handler.
 *
 * Returns -1 if the path does not resolve to a readable file.
 */
int tiku_vfs_read(const char *path, char *buf, size_t max)
{
    const tiku_vfs_node_t *node;

    node = tiku_vfs_resolve(path);
    if (node == NULL || node->type != TIKU_VFS_FILE || node->read == NULL) {
        return -1;
    }

    return node->read(buf, max);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Resolve a path and invoke the file's write handler.
 *
 * Returns -1 if the path does not resolve to a writable file.
 */
int tiku_vfs_write(const char *path, const char *data, size_t len)
{
    const tiku_vfs_node_t *node;

    node = tiku_vfs_resolve(path);
    if (node == NULL || node->type != TIKU_VFS_FILE || node->write == NULL) {
        return -1;
    }

    return node->write(data, len);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief List the children of a directory node.
 *
 * Resolves the path, verifies it is a directory, then invokes
 * @p callback for each child with name, type, and the caller's
 * context pointer.  Returns -1 if the path is not a directory.
 */
int tiku_vfs_list(const char *path, tiku_vfs_list_fn callback, void *ctx)
{
    const tiku_vfs_node_t *node;
    uint8_t i;

    node = tiku_vfs_resolve(path);
    if (node == NULL || node->type != TIKU_VFS_DIR) {
        return -1;
    }

    for (i = 0; i < node->child_count; i++) {
        callback(&node->children[i], ctx);
    }

    return 0;
}
