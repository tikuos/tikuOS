/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs.c - Tree walker, path resolver, read/write dispatch, watch
 *
 * The VFS core is intentionally minimal: it resolves slash-separated
 * paths against a static tree of nodes and dispatches to read/write
 * handler functions.  No malloc, no string copies, no inodes.
 *
 * It also owns the watch layer — the namespace as event bus: a
 * fixed table of (node, process) subscriptions, rung automatically
 * on every successful write and explicitly by drivers via
 * tiku_vfs_notify().  See the WATCH section in tiku_vfs.h for the
 * full semantics.
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
#include <kernel/process/tiku_process.h>
#include <hal/tiku_cpu.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static const tiku_vfs_node_t *vfs_root;

/**
 * @brief One watch subscription: which node rings which process.
 *
 * A slot is free when @ref node is NULL.  Slots live in SRAM —
 * subscriptions are per-boot state, re-established by their owners
 * at init (the rules engine re-arms after every rule mutation).
 */
typedef struct {
    const tiku_vfs_node_t *node;   /**< watched node (NULL = free) */
    struct tiku_process   *proc;   /**< event receiver             */
} tiku_vfs_watch_slot_t;

/**
 * The watch table.  Fixed capacity (TIKU_VFS_WATCH_MAX, default 8);
 * linear scans throughout — at this size a scan is a handful of
 * compares, cheaper than any indexing structure's bookkeeping.
 *
 * Concurrency model: mutation (watch/unwatch) runs in process
 * context inside tiku_atomic_enter()/exit(), so an ISR calling
 * tiku_vfs_notify() can never observe a half-written slot.  Scans
 * (notify) take no lock: processes are cooperative (no preemption
 * between them) and ISR scans see either a fully-valid or a free
 * slot thanks to the masked mutation.
 */
static tiku_vfs_watch_slot_t watch_table[TIKU_VFS_WATCH_MAX];

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
 *
 * On success (handler returned 0) the node's watchers are rung via
 * tiku_vfs_notify() — this is the automatic trigger path that makes
 * every shell, BASIC, and network write observable without the
 * writer knowing watchers exist.  Failed writes (handler returned
 * -1) do not notify: the node did not change.
 */
int tiku_vfs_write(const char *path, const char *data, size_t len)
{
    const tiku_vfs_node_t *node;
    int rc;

    node = tiku_vfs_resolve(path);
    if (node == NULL || node->type != TIKU_VFS_FILE || node->write == NULL) {
        return -1;
    }

    rc = node->write(data, len);
    if (rc == 0) {
        tiku_vfs_notify(node);
    }
    return rc;
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

/*---------------------------------------------------------------------------*/
/* WATCH — change notification on nodes                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Subscribe a process to changes of a FILE node.
 *
 * Resolves the path once, at subscription time: the tree is static,
 * so the resulting node pointer is a stable subscription key and no
 * per-event path walk is ever needed.  The duplicate check makes
 * subscription idempotent — consumers that re-arm wholesale (drop
 * everything, re-subscribe every interest) need no bookkeeping
 * about what was already watched.
 *
 * Table mutation runs inside tiku_atomic_enter()/exit() so a
 * concurrent ISR notify scan can never see a torn slot (node and
 * proc are written as a masked pair).
 *
 * @param path  Absolute path to a FILE node
 * @param p     Receiving process
 * @return Slot index (>= 0), or -1 on bad path, non-FILE node,
 *         NULL process, or full table
 */
int8_t tiku_vfs_watch(const char *path, struct tiku_process *p)
{
    const tiku_vfs_node_t *node;
    int8_t free_slot = -1;
    int8_t i;

    if (p == NULL) {
        return -1;
    }
    node = tiku_vfs_resolve(path);
    if (node == NULL || node->type != TIKU_VFS_FILE) {
        return -1;
    }

    tiku_atomic_enter();
    for (i = 0; i < TIKU_VFS_WATCH_MAX; i++) {
        if (watch_table[i].node == node && watch_table[i].proc == p) {
            tiku_atomic_exit();
            return i;               /* idempotent: already watching */
        }
        if (watch_table[i].node == NULL && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot >= 0) {
        watch_table[free_slot].proc = p;
        watch_table[free_slot].node = node;
    }
    tiku_atomic_exit();

    return free_slot;               /* -1 when the table is full */
}

/**
 * @brief Remove one (path, process) subscription.
 *
 * Clearing @ref node frees the slot; the masked section keeps the
 * clear atomic with respect to ISR notify scans.
 *
 * @param path  The watched path
 * @param p     The subscribed process
 * @return 0 when a subscription was removed, -1 when none matched
 */
int8_t tiku_vfs_unwatch(const char *path, struct tiku_process *p)
{
    const tiku_vfs_node_t *node;
    int8_t rc = -1;
    int8_t i;

    node = tiku_vfs_resolve(path);
    if (node == NULL || p == NULL) {
        return -1;
    }

    tiku_atomic_enter();
    for (i = 0; i < TIKU_VFS_WATCH_MAX; i++) {
        if (watch_table[i].node == node && watch_table[i].proc == p) {
            watch_table[i].node = NULL;
            watch_table[i].proc = NULL;
            rc = 0;
            break;
        }
    }
    tiku_atomic_exit();

    return rc;
}

/**
 * @brief Remove every subscription held by @p p.
 *
 * The re-arm primitive: consumers drop all their watches and
 * re-subscribe from current state, which is simpler and safer than
 * tracking which subscription belonged to which (possibly deleted)
 * interest.
 *
 * @param p  The subscribed process
 */
void tiku_vfs_unwatch_all(struct tiku_process *p)
{
    int8_t i;

    if (p == NULL) {
        return;
    }

    tiku_atomic_enter();
    for (i = 0; i < TIKU_VFS_WATCH_MAX; i++) {
        if (watch_table[i].proc == p) {
            watch_table[i].node = NULL;
            watch_table[i].proc = NULL;
        }
    }
    tiku_atomic_exit();
}

/**
 * @brief Ring the watchers of @p node.
 *
 * Posts TIKU_EVENT_VFS (data = the node pointer) to every process
 * subscribed to @p node.  ISR-safe: the scan is lock-free (see the
 * watch_table concurrency note) and tiku_process_post() is itself
 * ISR-safe.  Events are not coalesced — each trigger posts one
 * event per watcher, and the receiver reads the node for the
 * current value, so several queued events for one node degrade to
 * harmless re-reads.
 *
 * Called automatically by tiku_vfs_write() on success; drivers
 * whose node values change without a write (GPIO edges, sensor
 * thresholds) call it explicitly.
 *
 * @param node  The node that changed
 */
void tiku_vfs_notify(const tiku_vfs_node_t *node)
{
    int8_t i;

    if (node == NULL) {
        return;
    }

    for (i = 0; i < TIKU_VFS_WATCH_MAX; i++) {
        if (watch_table[i].node == node) {
            tiku_process_post(watch_table[i].proc, TIKU_EVENT_VFS,
                              (tiku_event_data_t)(uintptr_t)node);
        }
    }
}
