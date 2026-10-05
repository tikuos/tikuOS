/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs.h - virtual filesystem public API and types.
 *
 * Exposes device state as a tree of named paths backed by read/write handlers,
 * with no block storage and no inodes.  The shell, BASIC and the rules engine
 * reach a value by the same path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_H_
#define TIKU_VFS_H_

#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* STATUS CODES                                                              */
/*---------------------------------------------------------------------------*/
/*
 * VFS status codes.  Every error is negative, so `rc < 0` tests for failure
 * and the value says which one: the shell, BASIC or an agent driving the
 * namespace can tell "no such node" from "read-only" from "bad value".
 *
 * A read returns a byte count >= 0 on success; it is snprintf-style, so the
 * value is the full length of the rendering and a return >= the buffer size
 * means the text was truncated.  Writes and typed reads return TIKU_VFS_OK
 * (0) on success.
 *
 * E2BIG reports exhausted bounded capacity, such as a full boot-mount table;
 * text truncation is signalled by the snprintf-style length, not an error.
 * EIO reports a backend or hardware failure.
 */
enum {
    TIKU_VFS_OK      =  0,   /**< success                                    */
    TIKU_VFS_ERR     = -1,   /**< unspecified failure                        */
    TIKU_VFS_ENOENT  = -2,   /**< no such path / node                        */
    TIKU_VFS_EACCES  = -3,   /**< not readable / not writable / wrong type   */
    TIKU_VFS_EINVAL  = -4,   /**< malformed input or bad argument            */
    TIKU_VFS_ERANGE  = -5,   /**< value out of range / numeric overflow      */
    TIKU_VFS_E2BIG   = -6,   /**< bounded capacity exhausted                 */
    TIKU_VFS_EIO     = -7,   /**< backend / hardware error                    */
    TIKU_VFS_ECONFLICT = -9, /**< configuration revision/value conflict */
    TIKU_VFS_ESTALE = -10,  /**< expired request or storage incarnation */
    TIKU_VFS_EBUSY = -11,   /**< operation already in progress */
    TIKU_VFS_ENOTSUP = -12, /**< not available: hardware or feature absent,
                                 store not mounted, recovery not enrolled */
    TIKU_VFS_ECORRUPT = -13,/**< invalid/incompatible durable state */
    TIKU_VFS_EPERM   = -8    /**< denied by policy: caller lacks the node's
                                  required capability (see tiku_vfs_cap_t)    */
};

/*---------------------------------------------------------------------------*/
/* CAPABILITIES — who may write a node                                       */
/*---------------------------------------------------------------------------*/
/*
 * Every writable node may declare the capability a writer must hold
 * (tiku_vfs_node_t.req_cap), and every write runs under the caller
 * capability of the channel driving the VFS (tiku_vfs_caller_cap_set()).
 * tiku_vfs_write() grants a write iff (req_cap & ~caller_cap) == 0.  A write
 * or delete served by a dynamic directory's ops needs TIKU_VFS_CAP_FS.
 *
 * A req_cap of 0 (NONE) leaves a node open to every writer.  The caller cap
 * is TIKU_VFS_CAP_ALL unless the active shell I/O backend carries a narrower
 * one: a remote backend such as the BLE or TCP shell has NONE, so it is
 * refused the nodes that actuate hardware or touch system state.
 */
typedef uint8_t tiku_vfs_cap_t;

#define TIKU_VFS_CAP_NONE  0x00u  /**< no capability required / granted       */
#define TIKU_VFS_CAP_HW    0x01u  /**< actuate hardware: gpio, led, i2c, pins */
#define TIKU_VFS_CAP_SYS   0x02u  /**< system/safety control: watchdog, clock */
#define TIKU_VFS_CAP_FS    0x04u  /**< mutate persistent store: /data, tier   */
#define TIKU_VFS_CAP_NET   0x08u  /**< network config / credentials           */
#define TIKU_VFS_CAP_ALL   0xFFu  /**< full authority: console, kernel, init  */

/**
 * @brief Short, stable, machine-greppable name for a status code.
 *
 * For surfacing a failure legibly (shell "(ENOENT)", BASIC, agent parsing).
 *
 * @param status  A value returned by a tiku_vfs_* call.
 * @return e.g. "ENOENT", "ERANGE"; "OK" for 0; "E?" for any unknown value.
 *         Never NULL.
 */
const char *tiku_vfs_strerror(int status);

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
 * @return bytes written (snprintf-style: the full length), or a negative
 *         TIKU_VFS_* status on error
 */
typedef int (*tiku_vfs_read_fn)(char *buf, size_t max);

/**
 * @brief Write handler: receive string value
 * @return TIKU_VFS_OK (0) on success, or a negative TIKU_VFS_* status on error
 *         (e.g. TIKU_VFS_EINVAL for malformed input, TIKU_VFS_ERANGE for an
 *         out-of-range value).  TIKU_VFS_ERR (-1) reports a failure without
 *         classifying it.
 */
typedef int (*tiku_vfs_write_fn)(const char *buf, size_t len);

/*---------------------------------------------------------------------------*/
/* TYPE DESCRIPTORS — machine-readable node metadata                         */
/*---------------------------------------------------------------------------*/
/*
 * Handlers render and accept human text.  A node may also carry a pointer to
 * a const descriptor of machine-readable metadata: value type, unit, range,
 * freshness and read cost.  desc == NULL means untyped, and nodes of one kind
 * share one descriptor.
 *
 * Descriptors drive the typed read (tiku_vfs_read_val()), the read cache
 * (fresh_ticks), the read policy (tiku_vfs_read_policy()) and the renderings
 * in tiku_vfs_desc_str() and tiku_vfs_manifest().  The range is rendered,
 * not enforced: no write is checked against vmin and vmax.
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

/** @brief Physical unit of a value; the integer is expressed in this unit. */
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

/** @brief How live a value is — what a read does. */
typedef enum {
    TIKU_VFS_FRESH_STATIC = 0, /**< Never changes after boot */
    TIKU_VFS_FRESH_CACHED,     /**< Changes; a read is cheap (counter/reg) */
    TIKU_VFS_FRESH_LIVE        /**< Sampled on read; costs energy (ADC/I2C) */
} tiku_vfs_fresh_t;

/** @brief What producing a value costs. */
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
/** Read changes state or starts an operation */
#define TIKU_VFS_DF_READ_EFFECT   0x0008u
/** Read removes data from a stream or queue */
#define TIKU_VFS_DF_READ_CONSUMES 0x0010u

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
 * @brief Sidecar metadata for a typed node (const).
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

/** @brief Build a plain descriptor carrying TIKU_VFS_DF_* flags @p fl. */
#define TIKU_VFS_DESC_FLAGS(vt, un, fr, ec, fl)                             \
    { (uint8_t)(vt), (uint8_t)(un), (uint8_t)(fr), (uint8_t)(ec),           \
      (fl), 0u, 0, 0, 0, 0 }

/** @brief Build a ranged descriptor (DF_RANGE set; vmin..vmax meaningful). */
#define TIKU_VFS_DESC_R(vt, un, fr, ec, lo, hi)                             \
    { (uint8_t)(vt), (uint8_t)(un), (uint8_t)(fr), (uint8_t)(ec),           \
      TIKU_VFS_DF_RANGE, 0u, 0, (int32_t)(lo), (int32_t)(hi), 0 }

/**
 * @brief Build a ranged descriptor with a freshness/cache window.
 *
 * @p ticks is the read-coalescing window in system ticks: the freshness cache
 * (kernel/vfs/tiku_vfs_cache.h) serves a cached value for up to this long.
 * Keep it under CACHE_MAX_AGE_S (30 s), past which every entry expires.
 */
#define TIKU_VFS_DESC_RF(vt, un, fr, ec, lo, hi, ticks)                     \
    { (uint8_t)(vt), (uint8_t)(un), (uint8_t)(fr), (uint8_t)(ec),           \
      TIKU_VFS_DF_RANGE, (uint16_t)(ticks), 0, (int32_t)(lo), (int32_t)(hi), 0 }

/*---------------------------------------------------------------------------*/
/* DYNAMIC DIRECTORIES — runtime-populated children (e.g. a file store)      */
/*---------------------------------------------------------------------------*/
/*
 * A DIR node may carry an optional `dyn` ops pointer.  Such a directory has
 * children that are not in the static children[] array but are resolved at
 * run time; the file store mounts /data this way.  read and write fall back
 * to the dyn ops only when a path does not resolve to a static node, and
 * list() enumerates the static children and then the dynamic ones.  A
 * dynamic child is addressed by name (the const node handlers carry no file
 * identity), so the ops take the name.
 */

/** @brief Per-name callback for tiku_vfs_dynops.list(). */
typedef void (*tiku_vfs_dyn_list_cb)(const char *name, void *ctx);

/** @brief Runtime child operations for a dynamic directory. */
typedef struct tiku_vfs_dynops {
    /** Enumerate every child by name. */
    void (*list)  (tiku_vfs_dyn_list_cb cb, void *ctx);
    /** Read a child; bytes rendered or a negative status. */
    int  (*read)  (const char *name, char *buf, size_t max);
    /** Write or create a child; 0 or a negative status. */
    int  (*write) (const char *name, const char *buf, size_t len);
    /** Delete a child; 0 or a negative status. */
    int  (*unlink)(const char *name);
    /** Optional: enumerate the immediate children under @p prefix ("" = root,
     *  "logs/" = a sub-folder), reporting virtual folders with a trailing '/'.
     *  NULL on a purely flat store -- the VFS then falls back to list(). */
    void (*list_dir)(const char *prefix, tiku_vfs_dyn_list_cb cb, void *ctx);
} tiku_vfs_dynops_t;

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
                                                    untyped */
    const struct tiku_vfs_dynops *dyn;         /**< Dynamic children; NULL =
                                                    static directory */
    tiku_vfs_cap_t               req_cap;       /**< Capability a writer must
                                                    hold; 0 (the default)
                                                    leaves the node open */
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
 * @brief Most subtrees tiku_vfs_mount() can attach.
 *
 * The default fits the driver registry: /sys/drivers, one /dev/<class>
 * directory for each of the eight driver classes, and eight drivers.
 */
#ifndef TIKU_VFS_MOUNT_MAX
#define TIKU_VFS_MOUNT_MAX 17
#endif

/**
 * @brief Attach a static subtree under an existing directory at boot.
 *
 * Nothing is copied or allocated and there is no unmount, so the node and
 * its children must outlive the VFS.  tiku_vfs_init() clears every mount.
 *
 * @param parent  Absolute path of a directory without dynamic children
 * @param node    Subtree root; its name must be free in @p parent
 * @return TIKU_VFS_OK; TIKU_VFS_ENOENT for a missing parent; TIKU_VFS_EACCES
 *         for a file or dynamic parent; TIKU_VFS_EINVAL for a malformed or
 *         already attached subtree; TIKU_VFS_ECONFLICT for a taken name;
 *         TIKU_VFS_E2BIG when TIKU_VFS_MOUNT_MAX mounts exist
 * @note Call from cooperative startup code, not from an interrupt or from
 *       inside a tiku_vfs_list() callback.
 */
int tiku_vfs_mount(const char *parent, const tiku_vfs_node_t *node);

/**
 * @brief Name what reading a node does, without calling its handler.
 *
 * "poll" is a plain read; "sample" wakes a peripheral or a bus; "consume"
 * drains data; "effect" changes state.  An untyped node is "unknown", not
 * assumed safe, and a node that is not a readable file is "none".
 *
 * @param node  Node to classify (NULL tolerated)
 * @return One of the names above; never NULL
 */
const char *tiku_vfs_read_policy(const tiku_vfs_node_t *node);

/**
 * @brief Read a node only when the read is a pure observation.
 *
 * Refuses, without calling the handler, any node whose policy is not "poll"
 * and any secret node.  tiku_vfs_read() still reads every readable node.
 *
 * @param path  Absolute path
 * @param buf   Output buffer
 * @param max   Buffer capacity; must be non-zero
 * @return Bytes rendered (snprintf-style); TIKU_VFS_ENOENT, TIKU_VFS_EINVAL
 *         for a bad buffer, or TIKU_VFS_EACCES for a refused node
 */
int tiku_vfs_read_passive(const char *path, char *buf, size_t max);

/**
 * @brief Extract the next segment of a slash-separated path.
 *
 * The one definition of TikuOS path lexing, shared by every segment walker so
 * they cannot drift: runs of slashes collapse, a trailing slash yields no
 * empty segment, and an all-slash remainder is the end of the path.
 *
 * @param p    Cursor into the path; advanced past the extracted
 *             segment (left on the terminating '/' or NUL).
 * @param seg  Receives the start of the segment (not NUL-terminated).
 * @param len  Receives the segment length in bytes (always > 0 on a
 *             1 return).
 * @return 1 if a segment was extracted, 0 at end of path.
 */
static inline int
tiku_vfs_next_segment(const char **p, const char **seg, size_t *len)
{
    while (**p == '/') (*p)++;               /* collapse slash runs   */
    if (**p == '\0') return 0;               /* end / trailing slash  */
    *seg = *p;
    while (**p != '/' && **p != '\0') (*p)++;
    *len = (size_t)(*p - *seg);
    return 1;
}

/**
 * @brief Resolve a path to its node
 * @param path  Absolute path (must start with "/")
 * @return Matching node, or NULL if not found
 */
const tiku_vfs_node_t *tiku_vfs_resolve(const char *path);

/**
 * @brief Read from a path
 *
 * A path with no static node falls back to its dynamic directory's read op.
 *
 * @param path  Absolute path to a FILE node
 * @param buf   Output buffer
 * @param max   Buffer capacity
 * @return Bytes rendered (snprintf-style); TIKU_VFS_ENOENT when nothing
 *         matches, TIKU_VFS_EACCES for a node that is not a readable file,
 *         or the handler's negative status
 */
int tiku_vfs_read(const char *path, char *buf, size_t max);

/**
 * @brief Read directly from a resolved node, skipping the path walk.
 *
 * For callers that already hold a node pointer (a watch event delivers one;
 * the rules engine and `watch` cache one at arm time).  Validates and fails
 * as tiku_vfs_read() does for a static node.
 *
 * @param node  Node to read; NULL yields TIKU_VFS_ENOENT
 * @param buf   Output buffer
 * @param max   Buffer capacity
 * @return Bytes rendered (snprintf-style), TIKU_VFS_EACCES for a node that
 *         is not a readable file, or the handler's negative status
 */
int tiku_vfs_read_node(const tiku_vfs_node_t *node, char *buf, size_t max);

/**
 * @brief The node whose read or write handler the VFS is running.
 *
 * Set by tiku_vfs_read_node() and tiku_vfs_write() for the length of the
 * handler call, so one handler can serve several nodes and tell them apart.
 *
 * @return The node being served, or NULL outside a handler call
 */
const tiku_vfs_node_t *tiku_vfs_serving(void);

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
 * Requires a descriptor: a native producer (desc.read_val) is used when
 * present, otherwise the text handler is rendered once and decoded per the
 * declared type.  Read untyped and STR nodes as text instead.
 *
 * @param path  Absolute path to a typed FILE node
 * @param out   Decoded value (always zeroed first; vtype=NONE on failure)
 * @return TIKU_VFS_OK; TIKU_VFS_ERR for an untyped or STR node or for text
 *         that does not decode; TIKU_VFS_EINVAL for a NULL @p out;
 *         TIKU_VFS_ENOENT; TIKU_VFS_EACCES; or the read's negative status
 */
int tiku_vfs_read_val(const char *path, tiku_vfs_val_t *out);

/** @brief By-node form of tiku_vfs_read_val() (skips the path walk). */
int tiku_vfs_read_val_node(const tiku_vfs_node_t *node, tiku_vfs_val_t *out);

/**
 * @brief Render a node's descriptor as a one-line human/manifest string.
 *
 * e.g. "u32 Hz cost=free fresh=static read=poll\n", or "i32 mC cost=periph
 * fresh=live read=sample [-40000..125000]\n".  "untyped\n" when the node
 * has no descriptor.  Newline-terminated, snprintf-style return.
 *
 * @return Bytes that would be written (>=0), or -1 on bad args.
 */
int tiku_vfs_desc_str(const tiku_vfs_node_t *node, char *buf, size_t max);

/**
 * @brief Write to a path
 *
 * A path with no static node writes or creates a child of its dynamic
 * directory, which needs TIKU_VFS_CAP_FS.  A successful write notifies the
 * node, or a dynamic child's directory (tiku_vfs_notify()).
 *
 * @param path  Absolute path to a writable FILE node
 * @param data  Data to write; NULL only with @p len 0
 * @param len   Data length
 * @return TIKU_VFS_OK; TIKU_VFS_EINVAL for NULL @p data with a length;
 *         TIKU_VFS_ENOENT when neither a static node nor a dynamic directory
 *         takes the write; TIKU_VFS_EACCES for a node that is not writable;
 *         TIKU_VFS_EPERM when the caller lacks the capability; or the
 *         handler's negative status
 */
int tiku_vfs_write(const char *path, const char *data, size_t len);

/*---------------------------------------------------------------------------*/
/* CALLER CAPABILITY — the ambient trust of the channel driving the VFS      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Set the ambient caller capability; returns the previous value.
 *
 * One control plane (the shell) drives every VFS write, so the cap is an
 * ambient word rather than a per-call argument.  It defaults to
 * TIKU_VFS_CAP_ALL; tiku_shell_io_set_backend() sets it to the backend's cap.
 *
 * @param cap  New ambient capability
 * @return     Previous value (save it to restore on the way out)
 */
tiku_vfs_cap_t tiku_vfs_caller_cap_set(tiku_vfs_cap_t cap);

/** @brief Current ambient caller capability. */
tiku_vfs_cap_t tiku_vfs_caller_cap_get(void);

/**
 * @brief Delete a file at @p path.
 *
 * Only dynamic directories (a file store mounted via dynops) support removal,
 * and the caller needs TIKU_VFS_CAP_FS, as for creating a file.  On success
 * the change ring records TIKU_VFS_OP_CHANGED for the directory.
 *
 * @param path  Absolute path to a dynamic FILE node
 * @return 0 on success, TIKU_VFS_EPERM without TIKU_VFS_CAP_FS, or -1 when
 *         the path is not a removable file
 */
int tiku_vfs_unlink(const char *path);

/**
 * @brief List directory contents
 * @param path      Absolute path to a DIR node
 * @param callback  Called once per child
 * @param ctx       User context passed to callback
 * @return 0 on success, -1 on error (not found or not a directory)
 */
int tiku_vfs_list(const char *path, tiku_vfs_list_fn callback, void *ctx);

/**
 * @brief Is @p path a directory?
 *
 * True for a static DIR node and for a virtual sub-folder of a dynamic store
 * (path-as-name): a path with at least one child under it, or an mkdir marker.
 * False for a file or a non-existent path.  Used by `cd`.
 *
 * @return 1 if @p path is a directory, 0 otherwise.
 */
int tiku_vfs_is_dir(const char *path);

/*---------------------------------------------------------------------------*/
/* WATCH — change notification on nodes                                      */
/*---------------------------------------------------------------------------*/

/*
 * The namespace as event bus: a process subscribes to a FILE node
 * and receives TIKU_EVENT_VFS (data = the node pointer) whenever the
 * node changes.  Two trigger paths feed the same subscription:
 *
 *   1. Every successful tiku_vfs_write() notifies watchers of the
 *      written node, so shell, BASIC and network writes all ring them.
 *   2. Drivers whose values change without a write (a GPIO edge, a
 *      radio scan) call tiku_vfs_notify() explicitly.
 *
 * Node-pointer identity is the subscription key: static and mounted nodes
 * are never moved or freed, so their addresses are stable for the boot, and
 * the event's data field carries the same pointer back to the receiver
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
 * Context rules: tiku_vfs_notify() is ISR-safe.  Besides posting events
 * (tiku_process_post() is ISR-safe) it appends to the change ring and
 * drops the node's read-cache entry; watch-table and change-ring updates
 * are interrupt-masked.  watch/unwatch are process-context calls.
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
 * Resolves @p path now and stores the (node, process) pair in a free slot;
 * subscribing the same pair twice returns the existing slot.  Thereafter each
 * write and each tiku_vfs_notify() posts TIKU_EVENT_VFS to @p p, node as data.
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
 * The bulk form used on re-arm: drop everything, then re-subscribe from
 * scratch.
 *
 * @param p  The subscribed process
 */
void tiku_vfs_unwatch_all(struct tiku_process *p);

/**
 * @brief Ring the watchers of @p node and record a CHANGED event.
 *
 * Called by tiku_vfs_write() on success and by drivers whose node values
 * change without a write.  ISR-safe.  The change record and the read-cache
 * drop happen even when nobody watches the node.
 *
 * @param node  The node that changed (as returned by
 *              tiku_vfs_resolve())
 */
void tiku_vfs_notify(const tiku_vfs_node_t *node);

/*---------------------------------------------------------------------------*/
/* CHANGE RECORDS -- what changed, not merely that something did             */
/*---------------------------------------------------------------------------*/
/*
 * A watch event says only that a node was touched.  Each notify also appends
 * a record {node, opcode, sequence} to a fixed ring in SRAM, so a reader that
 * drains it learns which nodes changed without re-reading the namespace.
 * When the ring is full the oldest record is dropped and a counter rises; a
 * reader that sees drops re-reads instead of trusting the records.
 *
 * tiku_vfs_notify() records TIKU_VFS_OP_CHANGED, and nothing in the core
 * passes another opcode: a file created or deleted in a dynamic directory is
 * recorded as CHANGED on the directory.
 */

/** @brief What happened to a node. */
typedef enum {
    TIKU_VFS_OP_CHANGED = 0,   /**< value changed: a write, or a driver */
    TIKU_VFS_OP_CREATED,       /**< appeared in a dynamic directory     */
    TIKU_VFS_OP_REMOVED,       /**< gone from a dynamic directory       */
    TIKU_VFS_OP_MOVED          /**< same node, new name                 */
} tiku_vfs_op_t;

/** @brief Change-ring capacity, in records. */
#ifndef TIKU_VFS_EVENTS_MAX
#define TIKU_VFS_EVENTS_MAX  16
#endif

/** @brief One change record. */
typedef struct {
    const tiku_vfs_node_t *node;    /**< the node notified                  */
    uint8_t                op;      /**< tiku_vfs_op_t                      */
    uint16_t               seq;     /**< wraps; gaps mean records were lost */
} tiku_vfs_change_t;

/**
 * @brief Ring the watchers of @p node and record what happened.
 *
 * The opcode-carrying form of tiku_vfs_notify(), which is this with
 * TIKU_VFS_OP_CHANGED.  ISR-safe.
 *
 * @param node  The node that changed
 * @param op    What happened to it
 */
void tiku_vfs_notify_op(const tiku_vfs_node_t *node, tiku_vfs_op_t op);

/**
 * @brief Take the pending change records, oldest first.
 *
 * Draining is destructive: a record is delivered once, to whichever reader
 * takes it first.  Reading /sys/vfs/events drains the same ring.
 *
 * @param out  Destination array (NULL to discard)
 * @param max  Capacity of @p out
 * @return Records written
 */
uint8_t tiku_vfs_events_take(tiku_vfs_change_t *out, uint8_t max);

/**
 * @brief How many records are waiting.
 * @return Pending count in [0, TIKU_VFS_EVENTS_MAX]
 */
uint8_t tiku_vfs_events_pending(void);

/**
 * @brief How many records have been dropped because the ring was full.
 *
 * Non-zero tells a reader its picture is incomplete and it must re-read
 * rather than trust the records it did get.
 *
 * @return Cumulative drops since boot
 */
uint16_t tiku_vfs_events_dropped(void);

/**
 * @brief The stable identity of @p node, an opaque token.
 *
 * A static or mounted node keeps its address for the boot, so its token is
 * the same however it is reached; equality is the only operation on it.  A
 * dynamic entry's temporary list node has no stable token.
 *
 * @param node  Any node, or NULL
 * @return The token, or 0 for NULL
 */
uint32_t tiku_vfs_node_id(const tiku_vfs_node_t *node);

/*---------------------------------------------------------------------------*/
/* INTROSPECTION — read-only views of VFS state                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Suggested buffer size for tiku_vfs_path_of().
 *
 * Exceeds the deepest path in the stock tree, and tiku_vfs_mount() refuses
 * a subtree whose paths would not fit.
 */
#ifndef TIKU_VFS_PATH_MAX
#define TIKU_VFS_PATH_MAX  64
#endif

/* Scratch-buffer size for the core's internal path-prefix reconstruction
 * (parent-of / list / is-dir): a full path plus a little slack. */
#define TIKU_VFS_PATHBUF   (TIKU_VFS_PATH_MAX + 16)

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

/**
 * @brief Render the namespace, boot mounts included, as a manifest.
 *
 * One tab-separated line per node, after a `#`-prefixed header row:
 * `path  type  perms  meta  cap  id`.  Dynamic-directory children are not
 * walked -- capability metadata only.
 *
 * @param buf  Output buffer
 * @param max  Buffer capacity
 * @return Total manifest length in bytes; a value >= @p max means the
 *         manifest was truncated (snprintf-style)
 */
int tiku_vfs_manifest(char *buf, size_t max);

#endif /* TIKU_VFS_H_ */
