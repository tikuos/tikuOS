/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_watch.c - /sys/watch and /sys/vfs VFS nodes
 *
 * The namespace observing itself.  Two read-only subtrees rendered
 * from the VFS core's introspection accessors (tiku_vfs.c):
 *
 *   /sys/watch/used    watch slots in use
 *   /sys/watch/free    watch slots available
 *   /sys/watch/<i>     slot i: "<path> <process>" or "free"
 *   /sys/vfs/nodes     total nodes in the tree (dirs + files)
 *   /sys/vfs/depth     deepest path, in components
 *
 * /sys/watch is the diagnostic for the watch layer the way
 * /sys/persist is for the cell layer: `cat /sys/watch/used` answers
 * "is the 8-slot table filling up / leaking?", and the per-slot
 * nodes show exactly who subscribed to what — the wholesale
 * unwatch_all() re-arm and the watch command's self-heal both
 * become visible.  Reads cost a short table scan (used/free) or a
 * tree DFS (the per-slot path reverse-lookup, /sys/vfs); all are
 * cold, human-triggered paths.
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

#include "tiku_vfs_tree_watch.h"
#include "tiku.h"
#include <kernel/process/tiku_process.h>
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
 * A used slot becomes "<absolute-path> <process-name>", e.g.
 * "/dev/led0 shell"; a free slot becomes "free".  The node's path
 * is recovered with tiku_vfs_path_of() (a reverse-lookup), the
 * process name read from the slot's subscriber.  The deepest stock
 * path (~22 chars) plus a short process name sits well inside the
 * 64-byte read buffer the `cat`/`read` command supplies.
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

/*---------------------------------------------------------------------------*/
/* NODE TABLES                                                               */
/*---------------------------------------------------------------------------*/

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
const tiku_vfs_node_t tiku_vfs_tree_vfs_children[] = {
    { "nodes", TIKU_VFS_FILE, vfs_nodes_read, NULL, NULL, 0 },
    { "depth", TIKU_VFS_FILE, vfs_depth_read, NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_vfs_children) /
               sizeof(tiku_vfs_tree_vfs_children[0])
               == TIKU_VFS_TREE_VFS_NCHILD,
               "TIKU_VFS_TREE_VFS_NCHILD out of sync");
