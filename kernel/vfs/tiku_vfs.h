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
/* TYPE DESCRIPTORS — machine-readable node metadata                         */
/*---------------------------------------------------------------------------*/
/*
 * The node struct stays minimal: handlers render and accept human
 * text.  A node may ALSO carry one const pointer to a descriptor —
 * a sidecar of machine-readable metadata (value type, unit, range,
 * freshness, energy cost) that lives in rodata/FRAM and costs zero
 * SRAM.  desc == NULL means "untyped": every existing node keeps
 * working unchanged, and only nodes that opt in pay the (shared,
 * const) descriptor.
 *
 * Descriptors are the substrate for the machine-facing layers built
 * on top of the namespace: a binary read path (tiku_vfs_read_val), a
 * freshness/energy-aware read cache (desc.fresh + desc.fresh_ticks),
 * write validation (desc.vmin/vmax), a self-describing manifest for
 * remote/agent consumers (tiku_vfs_desc_str), and per-node energy
 * accounting (desc.ecost).
 */

/** @brief How to interpret a node's value. */
typedef enum {
    TIKU_VFS_T_NONE = 0,   /**< Untyped / opaque text (default) */
    TIKU_VFS_T_U32,        /**< Unsigned integer */
    TIKU_VFS_T_I32,        /**< Signed integer */
    TIKU_VFS_T_BOOL,       /**< Boolean (0/1) */
    TIKU_VFS_T_FIXED,      /**< Fixed-point integer; see desc.scale */
    TIKU_VFS_T_STR         /**< Free text (machine path falls back to read) */
} tiku_vfs_vtype_t;

/** @brief Physical unit of a value (the integer IS expressed in this unit). */
typedef enum {
    TIKU_VFS_U_NONE = 0,
    TIKU_VFS_U_BOOL,
    TIKU_VFS_U_COUNT,
    TIKU_VFS_U_BYTES,
    TIKU_VFS_U_SECONDS,
    TIKU_VFS_U_MILLIS,
    TIKU_VFS_U_TICKS,
    TIKU_VFS_U_HERTZ,
    TIKU_VFS_U_MILLIVOLTS,
    TIKU_VFS_U_MILLIAMPS,
    TIKU_VFS_U_CELSIUS,
    TIKU_VFS_U_MILLICELSIUS,
    TIKU_VFS_U_PERCENT,
    TIKU_VFS_U_ADC_RAW,
    TIKU_VFS_U_MICROJOULES
} tiku_vfs_unit_t;

/** @brief How live a value is — what a read actually does. */
typedef enum {
    TIKU_VFS_FRESH_STATIC = 0, /**< Never changes after boot */
    TIKU_VFS_FRESH_CACHED,     /**< Changes, but a read is cheap (counter/reg) */
    TIKU_VFS_FRESH_LIVE        /**< Sampled on read; costs energy (ADC/I2C) */
} tiku_vfs_fresh_t;

/** @brief What producing a value costs — the seed of energy accounting. */
typedef enum {
    TIKU_VFS_E_FREE = 0,   /**< Register / SRAM read, ~free */
    TIKU_VFS_E_CHEAP,      /**< A few cycles, no peripheral wake */
    TIKU_VFS_E_PERIPH,     /**< Wakes a peripheral (ADC + reference) */
    TIKU_VFS_E_BUS         /**< Off-chip bus transaction (I2C/SPI) */
} tiku_vfs_ecost_t;

/** @brief Descriptor flag bits. */
#define TIKU_VFS_DF_NONE    0x0000u
#define TIKU_VFS_DF_RANGE   0x0001u  /**< vmin/vmax are meaningful */
#define TIKU_VFS_DF_HEX     0x0002u  /**< Natural rendering is hex */
#define TIKU_VFS_DF_SECRET  0x0004u  /**< Hide on remote/agent channels */

/**
 * @brief A decoded, machine-usable node value.
 *
 * Produced by tiku_vfs_read_val(); the union member to read is
 * selected by @ref vtype.  STR values are not decoded here — use the
 * text read path for those.
 */
typedef struct {
    uint8_t  vtype;   /**< tiku_vfs_vtype_t; NONE if undecodable */
    uint8_t  unit;    /**< tiku_vfs_unit_t (copied from the descriptor) */
    int16_t  scale;   /**< Decimal exponent for FIXED (value = raw*10^scale) */
    union {
        uint32_t u;   /**< U32 / FIXED / BOOL magnitude */
        int32_t  i;   /**< I32 */
    } as;
} tiku_vfs_val_t;

/**
 * @brief Sidecar metadata for a typed node (const; rodata/FRAM).
 *
 * Optional native producer @ref read_val short-circuits the text path
 * for hot machine-to-machine reads; when NULL, tiku_vfs_read_val()
 * renders the node's text handler and decodes it per @ref vtype.
 */
typedef struct tiku_vfs_desc {
    uint8_t  vtype;       /**< tiku_vfs_vtype_t */
    uint8_t  unit;        /**< tiku_vfs_unit_t */
    uint8_t  fresh;       /**< tiku_vfs_fresh_t */
    uint8_t  ecost;       /**< tiku_vfs_ecost_t */
    uint16_t flags;       /**< TIKU_VFS_DF_* */
    uint16_t fresh_ticks; /**< Cache window in system ticks; 0 = always live */
    int16_t  scale;       /**< Decimal exponent for FIXED; else 0 */
    int32_t  vmin;        /**< Range low  (valid iff DF_RANGE) */
    int32_t  vmax;        /**< Range high (valid iff DF_RANGE) */
    int (*read_val)(tiku_vfs_val_t *out); /**< Native producer, or NULL */
} tiku_vfs_desc_t;

/** @brief Build a plain descriptor (no range, no caching, text-decoded). */
#define TIKU_VFS_DESC(vt, un, fr, ec)                                       \
    { (uint8_t)(vt), (uint8_t)(un), (uint8_t)(fr), (uint8_t)(ec),           \
      TIKU_VFS_DF_NONE, 0u, 0, 0, 0, 0 }

/** @brief Build a ranged descriptor (DF_RANGE set; vmin..vmax meaningful). */
#define TIKU_VFS_DESC_R(vt, un, fr, ec, lo, hi)                             \
    { (uint8_t)(vt), (uint8_t)(un), (uint8_t)(fr), (uint8_t)(ec),           \
      TIKU_VFS_DF_RANGE, 0u, 0, (int32_t)(lo), (int32_t)(hi), 0 }

/**
 * @brief Build a ranged descriptor with a freshness/cache window.
 *
 * @p ticks is the read-coalescing window in system ticks (the freshness
 * cache serves a cached value for up to this long); see
 * kernel/vfs/tiku_vfs_cache.h.  Keep it well under a few seconds so the
 * cache's wrap guard never false-expires it.
 */
#define TIKU_VFS_DESC_RF(vt, un, fr, ec, lo, hi, ticks)                     \
    { (uint8_t)(vt), (uint8_t)(un), (uint8_t)(fr), (uint8_t)(ec),           \
      TIKU_VFS_DF_RANGE, (uint16_t)(ticks), 0, (int32_t)(lo), (int32_t)(hi), 0 }

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
    const tiku_vfs_desc_t       *desc;         /**< Type descriptor; NULL =
                                                    untyped (back-compat) */
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
 * @brief Read directly from a resolved node, skipping the path walk.
 *
 * For callers that already hold a node pointer (a watch event
 * delivers one as its payload; the rules engine and `watch` cache
 * one at arm time).  Same readable-FILE validation and error
 * contract as tiku_vfs_read(); a NULL @p node returns -1.
 *
 * @param node  Node to read (NULL tolerated → -1)
 * @param buf   Output buffer
 * @param max   Buffer capacity
 * @return Bytes written to buf, or -1 on error
 */
int tiku_vfs_read_node(const tiku_vfs_node_t *node, char *buf, size_t max);

/*---------------------------------------------------------------------------*/
/* TYPED ACCESS — descriptor-driven, machine-facing reads                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return a node's type descriptor.
 * @return The descriptor, or NULL if the node is untyped / NULL.
 */
const tiku_vfs_desc_t *tiku_vfs_desc_of(const tiku_vfs_node_t *node);

/**
 * @brief Read a node as a decoded, typed value.
 *
 * Requires a descriptor: a native producer (desc.read_val) is used
 * when present, otherwise the text handler is rendered once and
 * decoded per the declared type.  Untyped nodes — and STR types —
 * return -1; use the text path (tiku_vfs_read) for those.
 *
 * @param path  Absolute path to a typed FILE node
 * @param out   Decoded value (always zeroed first; vtype=NONE on failure)
 * @return 0 on success, -1 otherwise
 */
int tiku_vfs_read_val(const char *path, tiku_vfs_val_t *out);

/** @brief By-node form of tiku_vfs_read_val() (skips the path walk). */
int tiku_vfs_read_val_node(const tiku_vfs_node_t *node, tiku_vfs_val_t *out);

/**
 * @brief Render a node's descriptor as a one-line human/manifest string.
 *
 * e.g. "u32 Hz cost=free fresh=static\n", or "i32 mC [-40000..125000]
 * cost=periph fresh=live\n".  "untyped\n" when the node has no
 * descriptor.  Newline-terminated, snprintf-style return.
 *
 * @return Bytes that would be written (>=0), or -1 on bad args.
 */
int tiku_vfs_desc_str(const tiku_vfs_node_t *node, char *buf, size_t max);

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

/*---------------------------------------------------------------------------*/
/* INTROSPECTION — read-only views of VFS state                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Suggested buffer size for tiku_vfs_path_of().
 *
 * Comfortably exceeds the deepest path in the stock tree; sizes a
 * caller's scratch buffer without hard-coding a magic number.
 */
#ifndef TIKU_VFS_PATH_MAX
#define TIKU_VFS_PATH_MAX  64
#endif

/**
 * @brief Number of watch slots currently in use.
 * @return Used slots in [0, TIKU_VFS_WATCH_MAX]; free = MAX - used.
 */
uint8_t tiku_vfs_watch_used(void);

/**
 * @brief Read one watch slot's (node, process) pair.
 *
 * @param i     Slot index in [0, TIKU_VFS_WATCH_MAX)
 * @param node  Out: watched node (NULL to ignore)
 * @param proc  Out: subscribed process (NULL to ignore)
 * @return 0 if the slot is in use, -1 if free or out of range
 */
int8_t tiku_vfs_watch_get(uint8_t i, const tiku_vfs_node_t **node,
                          struct tiku_process **proc);

/**
 * @brief Reverse-resolve a node pointer to its absolute path.
 *
 * DFS from the root (the tree has no parent links); O(tree size),
 * for cold observability reads, not a hot path.  The root resolves
 * to "/".
 *
 * @param node  Node to name (must be in the tree)
 * @param buf   Output buffer (size >= TIKU_VFS_PATH_MAX recommended)
 * @param max   Buffer capacity
 * @return Path length, or -1 if not found / bad args
 */
int tiku_vfs_path_of(const tiku_vfs_node_t *node, char *buf, size_t max);

/**
 * @brief Total nodes in the tree (dirs + files), counted live.
 * @return Node count, or 0 before tiku_vfs_init()
 */
uint16_t tiku_vfs_count(void);

/**
 * @brief Deepest path in the tree, in components (root alone = 1).
 * @return Max depth, or 0 before tiku_vfs_init()
 */
uint8_t tiku_vfs_depth(void);

#endif /* TIKU_VFS_H_ */
