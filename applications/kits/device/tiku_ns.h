/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ns.h - the device's namespace, mirrored host-side.
 *
 * Static structure comes from `cat /sys/vfs/manifest` (path, kind, permissions,
 * descriptor, capability); values come from `cat` on demand and from `~` lines
 * when subscribed.  This is the model every window renders.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_NS_H_
#define TIKU_NS_H_

#include <stddef.h>
#include "tiku_session.h"

#define TIKU_NS_PATH_MAX  128
/* Wide enough for the descriptor plus the named tokens the manifest may
 * grow (scale=, f=); a truncated descriptor silently mis-renders a value. */
#define TIKU_NS_META_MAX   72
#define TIKU_VAL_MAX   512

/** @brief Permission bits parsed from the manifest's "rw" column. */
#define TIKU_NS_P_READ   0x1u
#define TIKU_NS_P_WRITE  0x2u

/** @brief One namespace node as the manifest describes it. */
typedef struct {
    char     path[TIKU_NS_PATH_MAX];
    char     name[64];                    /* last component, for display   */
    char     meta[TIKU_NS_META_MAX];    /* descriptor: "vtype,unit,..."  */
    /* One token today ("-", "hw", "sys", "fs", "net", or "cap" for a
     * combined mask).  Sized for the comma-separated form the manifest
     * would emit if a node ever required several: "hw,sys,fs,net". */
    char     cap[24];
    /* The node's identity as the device knows it (manifest rev 3+).  Zero
     * when the device predates the column -- a reader must then fall back to
     * the path, and cannot tell a rename from a different node. */
    unsigned id;
    unsigned perm;                        /* TIKU_NS_P_*                 */
    int      is_dir;
    int      depth;                       /* '/' components, root = 0      */
    int      subscribed;
    char     value[TIKU_VAL_MAX];    /* last read; empty until read   */
    int      value_valid;
    unsigned generation;                  /* bumped on every value change  */
} tiku_node_t;

typedef struct tiku_ns tiku_ns_t;

/** @brief Create a mirror over an established session (not owned). */
tiku_ns_t *tiku_ns_new(tiku_session_t *s);

void tiku_ns_free(tiku_ns_t *ns);

/**
 * @brief Fetch and parse the manifest, replacing any previous structure.
 *
 * @return Node count, or -1 if the device has no manifest node.
 */
int tiku_ns_load(tiku_ns_t *ns);

/** @brief Node count in the mirror. */
int tiku_ns_count(const tiku_ns_t *ns);

/** @brief Node by index (0..count-1), or NULL. */
const tiku_node_t *tiku_ns_at(const tiku_ns_t *ns, int i);

/** @brief Node by exact path, or NULL. */
/**
 * @brief The node carrying @p id, or NULL.
 *
 * A change record names an identity, not a path.  Turning one into the other
 * is the only way a reader can act on a record for something it is not
 * already showing.
 */
const tiku_node_t *tiku_ns_find_id(const tiku_ns_t *ns,
                                             unsigned id);

const tiku_node_t *tiku_ns_find(const tiku_ns_t *ns,
                                          const char *path);

/**
 * @brief Immediate children of @p path, in manifest order.
 *
 * @param out  Receives child pointers.
 * @return Count written, capped at @p max.
 */
int tiku_ns_children(const tiku_ns_t *ns, const char *path,
                          const tiku_node_t **out, int max);

/**
 * @brief Runtime children of a dynamic directory (/data), via `ls`.
 *
 * The manifest lists such directories but not their contents, which are data
 * rather than policy.
 *
 * @return Count written, or -1 on a link error.
 */
int tiku_ns_ls(tiku_ns_t *ns, const char *path,
                    char out[][64], int max);

/**
 * @brief As tiku_ns_ls(), also reporting which entries are directories.
 *
 * @param is_dir  Optional parallel array, 1 per directory entry.
 */
int tiku_ns_ls_kinds(tiku_ns_t *ns, const char *path,
                          char out[][64], char *is_dir, int max);

/** @brief One change the device recorded. */
typedef struct {
    char     op[12];     /* changed / created / removed / moved            */
    unsigned id;         /* the node's identity, as the manifest reports   */
    unsigned seq;
} tiku_ns_change_t;

/**
 * @brief Drain the device's change records.
 *
 * Reading /sys/vfs/events IS the drain, so this is one cheap round trip that
 * answers "did anything happen" -- and, when something did, exactly which
 * nodes.  A caller can then re-read only what moved instead of re-listing
 * the namespace on a timer.
 *
 * @param out      Destination (NULL to discard)
 * @param max      Capacity of @p out
 * @param dropped  Optional: records the device LOST to a full ring.  Non-zero
 *                 means the drained set is incomplete and the caller must
 *                 fall back to re-reading rather than trust it.
 * @return Records written, or -1 when the device has no such node (older
 *         firmware), which a caller must treat as "cannot tell" and poll.
 */
int tiku_ns_events(tiku_ns_t *ns, tiku_ns_change_t *out, int max,
                        unsigned *dropped);

/**
 * @brief Read a node's value into the mirror (`cat`).
 *
 * @return 0 on success, -1 if unreadable or the link failed.
 */
int tiku_ns_read(tiku_ns_t *ns, const char *path);

/**
 * @brief Write a value (`write`), then re-read it.
 *
 * @param err  Optional buffer for the device's refusal text.
 * @return 0 on success, -1 if refused (capability, range or absent node).
 */
int tiku_ns_write(tiku_ns_t *ns, const char *path, const char *value,
                       char *err, size_t errmax);

/**
 * @brief Subscribe to a node so writes elsewhere refresh it (`sub add`).
 *
 * Falls back to marking the node poll-only when the firmware lacks `sub`.
 * @return 0 when the device confirmed the subscription, 1 when polling
 *         instead, -1 on a link error.
 */
int tiku_ns_subscribe(tiku_ns_t *ns, const char *path);

/** @brief Drop a subscription (`sub del`).  0 on success. */
int tiku_ns_unsubscribe(tiku_ns_t *ns, const char *path);

/**
 * @brief Whether the manifest arrived cut by the device's read buffer.
 *
 * True means the mirror covers only the nodes that fit; tiku_ns_complete()
 * fills the rest from `ls`.
 */
int tiku_ns_truncated(const tiku_ns_t *ns);

/**
 * @brief Complete a truncated mirror by walking the tree with `ls`.
 *
 * Nodes discovered this way carry no descriptor or capability metadata; their
 * permissions are probed by a read.
 *
 * @return Nodes added, or -1 on a link error.
 */
int tiku_ns_complete(tiku_ns_t *ns, int max_depth);

/** @brief Whether the device answered `sub` at all (push vs poll UI). */
int tiku_ns_has_push(const tiku_ns_t *ns);

/**
 * @brief Service notifications; re-reads every node the device rang.
 *
 * @return Nodes refreshed, or -1 on a link error.
 */
int tiku_ns_pump(tiku_ns_t *ns, int timeout_ms);

#endif /* TIKU_NS_H_ */
