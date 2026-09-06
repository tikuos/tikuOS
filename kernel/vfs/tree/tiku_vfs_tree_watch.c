/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_watch.c - /sys/watch and /sys/vfs VFS nodes.
 *
 * The namespace observing itself: watch-table occupancy and per-slot contents,
 * plus tree node count and depth.  This is the diagnostic for subscription leaks.
 * Reads cost a table scan or a tree walk, both cold human-triggered paths.
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
 * Renders the number of occupied watch slots as a decimal line.
 * The cheap leak detector: on an idle device this should return to
 * its baseline after every watch/rule teardown.
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
 * a free slot becomes "free".  The path is recovered by tiku_vfs_path_of(), and
 * the deepest stock path plus a short name fits the 64-byte read buffer.
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
        /* Node not located in the tree (should not happen for a
         * live slot) — fall back to its leaf name so the slot stays
         * legible rather than blank. */
        return snprintf(buf, max, "%s %s\n", node->name, who);
    }

    return snprintf(buf, max, "%s %s\n", path, who);
}

/*
 * Per-slot read handlers.  Each binds its slot index the way the
 * /proc per-pid handlers do (tiku_proc_vfs.c): a macro emits one
 * thin forwarder per slot.  Written for the default eight-slot
 * table; the assert below fires if TIKU_VFS_WATCH_MAX changes so
 * this list and the children table are extended in lockstep.
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
 * Dumps the whole static namespace as a machine-readable, tab-separated table
 * (see tiku_vfs_manifest()) so an agent discovers every node -- path, type,
 * perms, and type descriptor -- in a single read.
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
 * The read-coalescing cache (kernel/vfs/tiku_vfs_cache.c) renders its
 * effectiveness here: a rising hit:miss ratio is the energy win made
 * visible (each hit is one ADC/bus access avoided).
 */

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
 * Renders the read-coalescing cache's cumulative hit count (each hit is
 * one sensor/bus access avoided).
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
 * Renders the read-coalescing cache's cumulative miss count (each miss
 * is a fresh sensor/bus access).
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
 * The change ring rendered as text.  Reading it is the drain, which is why
 * this is a node and not a shell mode: an agent already knows how to read a
 * file, and doing so does not take the shell away from anything else.
 *
 * One record per line: <op> <id> <seq>.  The trailing summary line reports
 * how many records have been LOST to a full ring since boot -- a reader that
 * sees it rise knows its picture is incomplete and must re-read rather than
 * trust what it just drained.
 */

/** Op names, indexed by tiku_vfs_op_t. */
static const char *const vfs_op_names[] = {
    "changed", "created", "removed", "moved"
};

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

/**
 * /sys/vfs directory table.  Exported for tiku_vfs_tree_sys.c; the
 * entry count travels as TIKU_VFS_TREE_VFS_NCHILD.
 */
/*
 * Manifest schema version -- bump when the manifest LINE FORMAT changes so an
 * external agent consuming /sys/vfs/manifest can pin or adapt instead of
 * silently mis-parsing.  rev 3 = the six-column form (path type perms
 * meta cap id), which adds the stable per-node identity; rev 2 was the
 * five-column form; rev 1 was the pre-capability four-column form.
 */
#define TIKU_VFS_MANIFEST_REV  3u

/**
 * @brief Read handler for /sys/vfs/manifest_rev.
 *
 * Renders the manifest schema version (TIKU_VFS_MANIFEST_REV) so an
 * agent consuming /sys/vfs/manifest can pin or adapt to the line format.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Bytes written (snprintf-style)
 */
static int vfs_manifest_rev_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)TIKU_VFS_MANIFEST_REV);
}

const tiku_vfs_node_t tiku_vfs_tree_vfs_children[] = {
    { "nodes",        TIKU_VFS_FILE, vfs_nodes_read,        NULL, NULL, 0 },
    { "depth",        TIKU_VFS_FILE, vfs_depth_read,        NULL, NULL, 0 },
    { "manifest",     TIKU_VFS_FILE, vfs_manifest_read,     NULL, NULL, 0 },
    { "manifest_rev", TIKU_VFS_FILE, vfs_manifest_rev_read, NULL, NULL, 0 },
    { "events",       TIKU_VFS_FILE, vfs_events_read,       NULL, NULL, 0 },
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
