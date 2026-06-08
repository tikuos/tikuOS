/*
 * Tiku Operating System v0.05
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

/*---------------------------------------------------------------------------*/
/* WATCH — change notification on nodes                                      */
/*---------------------------------------------------------------------------*/

/*
 * The namespace as event bus: a process subscribes to a FILE node
 * and receives TIKU_EVENT_VFS (data = the node pointer) whenever the
 * node changes.  Two trigger paths feed the same subscription:
 *
 *   1. Every successful tiku_vfs_write() notifies watchers of the
 *      written node automatically — shell writes, BASIC writes and
 *      network writes all ring for free.
 *   2. Drivers whose values change without a write (a GPIO edge, a
 *      sensor threshold) call tiku_vfs_notify() explicitly.
 *
 * Node-pointer identity is the subscription key: the tree is static,
 * so node addresses are stable for the life of the system, and the
 * event's data field carries the same pointer back to the receiver
 * for dispatch.  The watch table is a fixed array of slots in SRAM
 * (subscriptions are per-boot; processes re-subscribe at init).
 *
 * Delivery semantics: one event per trigger, no coalescing — the
 * event means "this node was touched", and the receiver reads the
 * node for the current value.  Several queued events for one node
 * are harmless re-reads.  An event is posted even when a write
 * stored the same value as before (writes are not compared against
 * prior content).
 *
 * Context rules: tiku_vfs_notify() is ISR-safe (it only scans the
 * table and posts events; tiku_process_post() is ISR-safe, and
 * table mutation is interrupt-masked).  watch/unwatch are
 * process-context calls.
 */

/** Forward declaration — receivers are kernel processes */
struct tiku_process;

/** @brief Watch-table capacity (subscription slots) */
#ifndef TIKU_VFS_WATCH_MAX
#define TIKU_VFS_WATCH_MAX  8
#endif

/**
 * @brief Subscribe a process to changes of a FILE node.
 *
 * Resolves @p path now and stores the (node, process) pair in a
 * free watch slot.  Subscribing the same pair twice is idempotent
 * and returns the existing slot.  From then on, every successful
 * write to the node — and every explicit tiku_vfs_notify() on it —
 * posts TIKU_EVENT_VFS to @p p with the node pointer as event data.
 *
 * @param path  Absolute path to a FILE node
 * @param p     Receiving process
 * @return Slot index (>= 0), or -1 on bad path, non-FILE node,
 *         NULL process, or full table
 */
int8_t tiku_vfs_watch(const char *path, struct tiku_process *p);

/**
 * @brief Remove one (path, process) subscription.
 *
 * @param path  The watched path
 * @param p     The subscribed process
 * @return 0 when a subscription was removed, -1 when none matched
 */
int8_t tiku_vfs_unwatch(const char *path, struct tiku_process *p);

/**
 * @brief Remove every subscription held by @p p.
 *
 * The bulk form used on re-arm (drop everything, re-subscribe from
 * scratch) and on process teardown.
 *
 * @param p  The subscribed process
 */
void tiku_vfs_unwatch_all(struct tiku_process *p);

/**
 * @brief Ring the watchers of @p node.
 *
 * Called automatically by tiku_vfs_write() on success; called
 * explicitly by drivers whose node values change without a write.
 * ISR-safe.  No-op when nobody watches the node.
 *
 * @param node  The node that changed (as returned by
 *              tiku_vfs_resolve())
 */
void tiku_vfs_notify(const tiku_vfs_node_t *node);

#endif /* TIKU_VFS_H_ */
