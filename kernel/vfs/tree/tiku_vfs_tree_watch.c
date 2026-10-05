/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_watch.c - /sys/watch and /sys/vfs VFS nodes.
 *
 * Watch-table occupancy and per-slot contents, plus node count, depth,
 * manifest, change ring and read-cache counters.  Reading /sys/vfs/events
 * drains the ring.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_watch.h"
#include "tiku.h"
#include <kernel/process/tiku_process.h>
#include <kernel/vfs/tiku_vfs_cache.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /sys/watch/used, /sys/watch/free                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/watch/used.
 *
 * Renders the number of occupied watch slots as a decimal line.  A count
 * that stays above its idle baseline after every watch and rule is torn
 * down means a subscription was never released.
 *
 * @param buf  Output buffer
 * @param max  Buffer capacity
 * @return Bytes written, or -1 on error
 */
static int
watch_used_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)tiku_vfs_watch_used());
}

/**
 * @brief Read handler for /sys/watch/free.
 *
 * Renders the number of free slots (TIKU_VFS_WATCH_MAX minus used)
 * as a decimal line: how many more subscriptions the table accepts
 * before tiku_vfs_watch() starts returning -1.
 *
 * @param buf  Output buffer
 * @param max  Buffer capacity
 * @return Bytes written, or -1 on error
 */
static int
watch_free_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)(TIKU_VFS_WATCH_MAX - tiku_vfs_watch_used()));
}

/*---------------------------------------------------------------------------*/
/* /sys/watch/<i> — one node per watch slot                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Render one watch slot.
 *
 * A used slot becomes "<absolute-path> <process-name>", e.g. "/dev/led0 shell";
 * a free slot becomes "free".  tiku_vfs_path_of() recovers the path into a
 * TIKU_VFS_PATH_MAX buffer.
 *
 * @param i    Watch slot index
 * @param buf  Output buffer
 * @param max  Buffer capacity
 * @return Bytes written, or -1 on error
 */
static int
watch_slot_read(uint8_t i, char *buf, size_t max)
{
    const tiku_vfs_node_t *node;
    struct tiku_process   *proc = NULL;
    char                   path[TIKU_VFS_PATH_MAX];
    const char            *who;

    if (tiku_vfs_watch_get(i, &node, &proc) != 0) {
        return snprintf(buf, max, "free\n");
    }

    who = (proc != NULL && proc->name != NULL) ? proc->name : "?";

    if (tiku_vfs_path_of(node, path, sizeof(path)) < 0) {
        /* A node the tree walk does not find is shown by its own name. */
        return snprintf(buf, max, "%s %s\n", node->name, who);
    }

    return snprintf(buf, max, "%s %s\n", path, who);
}

/*
 * Per-slot read handlers: the macro emits one forwarder per slot index.  The
 * list covers eight slots, and the assert below fails the build when
 * TIKU_VFS_WATCH_MAX is not 8.
 */
#define WATCH_SLOT(idx)                                          \
    static int watch_slot_##idx##_read(char *buf, size_t max)   \
    {                                                           \
        return watch_slot_read(idx, buf, max);                  \
    }

WATCH_SLOT(0) WATCH_SLOT(1) WATCH_SLOT(2) WATCH_SLOT(3)
WATCH_SLOT(4) WATCH_SLOT(5) WATCH_SLOT(6) WATCH_SLOT(7)

_Static_assert(TIKU_VFS_WATCH_MAX == 8,
               "watch slot table is written for 8 slots — extend the "
               "WATCH_SLOT() list and tiku_vfs_tree_watch_children[] "
               "if TIKU_VFS_WATCH_MAX changes");

/*---------------------------------------------------------------------------*/
/* /sys/vfs/nodes, /sys/vfs/depth                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/vfs/nodes.
 *
 * Renders the live total node count (directories + files) as a
 * decimal line — the size of the namespace this image exposes.
 *
 * @param buf  Output buffer
 * @param max  Buffer capacity
 * @return Bytes written, or -1 on error
 */
static int
vfs_nodes_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)tiku_vfs_count());
}

/**
 * @brief Read handler for /sys/vfs/depth.
 *
 * Renders the deepest path in components (root alone is 1) as a
 * decimal line.
 *
 * @param buf  Output buffer
 * @param max  Buffer capacity
 * @return Bytes written, or -1 on error
 */
static int
vfs_depth_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)tiku_vfs_depth());
}

/**
 * @brief Read handler for /sys/vfs/manifest.
 *
 * Renders tiku_vfs_manifest(): one tab-separated line per node, boot mounts
 * included, giving path, type, perms, descriptor, capability and id.
 *
 * @param buf  Output buffer
 * @param max  Buffer capacity
 * @return Total manifest length (snprintf-style; >= max means truncated)
 */
static int
vfs_manifest_read(char *buf, size_t max)
{
    return tiku_vfs_manifest(buf, max);
}

/*---------------------------------------------------------------------------*/
/* /sys/vfs/cache/{used,hits,misses} — freshness-cache observability         */
/*---------------------------------------------------------------------------*/
/*
 * Counters of the read-coalescing cache (kernel/vfs/tiku_vfs_cache.c).  A hit
 * is a read served without calling the node's handler.
 */

/** @brief Read handler for /sys/vfs/cache/used: occupied cache slots. */
static int
vfs_cache_used_read(char *buf, size_t max)
{
    uint8_t used = 0;
    tiku_vfs_cache_stats(NULL, NULL, &used);
    return snprintf(buf, max, "%u\n", (unsigned)used);
}

/**
 * @brief Read handler for /sys/vfs/cache/hits.
 *
 * Renders the read-coalescing cache's cumulative hit count.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Bytes written (snprintf-style)
 */
static int
vfs_cache_hits_read(char *buf, size_t max)
{
    uint32_t hits = 0;
    tiku_vfs_cache_stats(&hits, NULL, NULL);
    return snprintf(buf, max, "%lu\n", (unsigned long)hits);
}

/**
 * @brief Read handler for /sys/vfs/cache/misses.
 *
 * Renders the read-coalescing cache's cumulative miss count: reads of a
 * cacheable node that called its handler.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Bytes written (snprintf-style)
 */
static int
vfs_cache_misses_read(char *buf, size_t max)
{
    uint32_t misses = 0;
    tiku_vfs_cache_stats(NULL, &misses, NULL);
    return snprintf(buf, max, "%lu\n", (unsigned long)misses);
}

/*---------------------------------------------------------------------------*/
/* /sys/vfs/events — what changed, drained by reading                        */
/*---------------------------------------------------------------------------*/
/*
 * The change ring rendered as text; reading the node drains the ring.  One
 * tab-separated record per line: <op> <id> <seq>, where id is the node's
 * manifest id.  A trailing "# <n> drained, <m> dropped" line gives the
 * records dropped to a full ring since boot; a rise in <m> between two reads
 * means records were lost between them.
 */

/** Op names, indexed by tiku_vfs_op_t. */
static const char *const vfs_op_names[] = {
    "changed", "created", "removed", "moved"
};

/** @brief Read handler for /sys/vfs/events: drains the change ring. */
static int
vfs_events_read(char *buf, size_t max)
{
    tiku_vfs_change_t rec[TIKU_VFS_EVENTS_MAX];
    uint8_t n, i;
    int off = 0;

    n = tiku_vfs_events_take(rec, (uint8_t)(sizeof rec / sizeof rec[0]));
    for (i = 0; i < n; i++) {
        const char *op = (rec[i].op < (sizeof vfs_op_names /
                                       sizeof vfs_op_names[0]))
                             ? vfs_op_names[rec[i].op] : "?";

        off += snprintf(buf + off, (off < (int)max) ? max - (size_t)off : 0u,
                        "%s\t%08lx\t%u\n", op,
                        (unsigned long)tiku_vfs_node_id(rec[i].node),
                        (unsigned)rec[i].seq);
    }
    off += snprintf(buf + off, (off < (int)max) ? max - (size_t)off : 0u,
                    "# %u drained, %u dropped\n", (unsigned)n,
                    (unsigned)tiku_vfs_events_dropped());
    return off;
}

/** @brief Read handler for /sys/vfs/events_pending — how many are waiting. */
static int
vfs_events_pending_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)tiku_vfs_events_pending());
}

/** @brief Read handler for /sys/vfs/events_dropped. */
static int
vfs_events_dropped_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)tiku_vfs_events_dropped());
}

/*---------------------------------------------------------------------------*/
/* NODE TABLES                                                               */
/*---------------------------------------------------------------------------*/

/** /sys/vfs/cache directory table — read-coalescing counters. */
static const tiku_vfs_node_t vfs_cache_children[] = {
    { "used",   TIKU_VFS_FILE, vfs_cache_used_read,   NULL, NULL, 0 },
    { "hits",   TIKU_VFS_FILE, vfs_cache_hits_read,   NULL, NULL, 0 },
    { "misses", TIKU_VFS_FILE, vfs_cache_misses_read, NULL, NULL, 0 },
};

/**
 * /sys/watch directory table: two summary counters plus one node
 * per watch slot.  Exported for tiku_vfs_tree_sys.c; the entry
 * count travels as TIKU_VFS_TREE_WATCH_NCHILD.
 */
const tiku_vfs_node_t tiku_vfs_tree_watch_children[] = {
    { "used", TIKU_VFS_FILE, watch_used_read,   NULL, NULL, 0 },
    { "free", TIKU_VFS_FILE, watch_free_read,   NULL, NULL, 0 },
    { "0",    TIKU_VFS_FILE, watch_slot_0_read, NULL, NULL, 0 },
    { "1",    TIKU_VFS_FILE, watch_slot_1_read, NULL, NULL, 0 },
    { "2",    TIKU_VFS_FILE, watch_slot_2_read, NULL, NULL, 0 },
    { "3",    TIKU_VFS_FILE, watch_slot_3_read, NULL, NULL, 0 },
    { "4",    TIKU_VFS_FILE, watch_slot_4_read, NULL, NULL, 0 },
    { "5",    TIKU_VFS_FILE, watch_slot_5_read, NULL, NULL, 0 },
    { "6",    TIKU_VFS_FILE, watch_slot_6_read, NULL, NULL, 0 },
    { "7",    TIKU_VFS_FILE, watch_slot_7_read, NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_watch_children) /
               sizeof(tiku_vfs_tree_watch_children[0])
               == TIKU_VFS_TREE_WATCH_NCHILD,
               "TIKU_VFS_TREE_WATCH_NCHILD out of sync");

/*
 * Manifest line-format version, raised whenever the line format of
 * tiku_vfs_manifest() changes.  Rev 4 is the six-column form (path type perms
 * meta cap id) whose typed meta ends in ";read=<policy>", then ";secret" for
 * a secret node.
 */
#define TIKU_VFS_MANIFEST_REV  4u

/* Reading /sys/vfs/events drains the change ring. */
static const tiku_vfs_desc_t desc_events = TIKU_VFS_DESC_FLAGS(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE,
    TIKU_VFS_DF_READ_CONSUMES);

/**
 * @brief Read handler for /sys/vfs/manifest_rev.
 *
 * Renders the manifest line-format version, TIKU_VFS_MANIFEST_REV.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Bytes written (snprintf-style)
 */
static int vfs_manifest_rev_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)TIKU_VFS_MANIFEST_REV);
}

/**
 * /sys/vfs directory table.  Exported for tiku_vfs_tree_sys.c; the
 * entry count travels as TIKU_VFS_TREE_VFS_NCHILD.
 */
const tiku_vfs_node_t tiku_vfs_tree_vfs_children[] = {
    { "nodes",        TIKU_VFS_FILE, vfs_nodes_read,        NULL, NULL, 0 },
    { "depth",        TIKU_VFS_FILE, vfs_depth_read,        NULL, NULL, 0 },
    { "manifest",     TIKU_VFS_FILE, vfs_manifest_read,     NULL, NULL, 0 },
    { "manifest_rev", TIKU_VFS_FILE, vfs_manifest_rev_read, NULL, NULL, 0 },
    { "events",       TIKU_VFS_FILE, vfs_events_read,       NULL, NULL, 0,
      &desc_events },
    { "events_pending", TIKU_VFS_FILE, vfs_events_pending_read,
      NULL, NULL, 0 },
    { "events_dropped", TIKU_VFS_FILE, vfs_events_dropped_read,
      NULL, NULL, 0 },
    { "cache",        TIKU_VFS_DIR,  NULL, NULL, vfs_cache_children,
      sizeof(vfs_cache_children) / sizeof(vfs_cache_children[0]) },
};

_Static_assert(sizeof(tiku_vfs_tree_vfs_children) /
               sizeof(tiku_vfs_tree_vfs_children[0])
               == TIKU_VFS_TREE_VFS_NCHILD,
               "TIKU_VFS_TREE_VFS_NCHILD out of sync");
