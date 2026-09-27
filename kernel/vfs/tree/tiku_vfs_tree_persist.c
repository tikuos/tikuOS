/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_persist.c - /sys/persist VFS nodes.
 *
 * Read-only: how many persist cells validated this boot, how many were primed
 * to defaults (0 on an established device, so non-zero means NVM content was
 * lost), how many this boot carried to new places, and where this image keeps
 * each cell (the manifest a tool compares with a new image before flashing).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_persist.h"
#include "tiku.h"
#include <kernel/memory/tiku_mem.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /sys/persist/cells, primed, moved, manifest                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/persist/cells.
 *
 * Renders the number of persist cells validated this boot ("4\n" with the
 * stock tree).  tiku_persist_cell_count() counts cell_init() calls rather than
 * a registry, so a cell whose init has not run yet does not appear.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
persist_cells_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_persist_cell_count());
}

/**
 * @brief Read handler for /sys/persist/primed.
 *
 * Renders how many cells had to be primed to defaults this boot as
 * a decimal line.  "0\n" is the healthy steady state; see the file
 * header for what non-zero means.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
persist_primed_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_persist_cell_primed());
}

/**
 * @brief Read handler for /sys/persist/moved.
 *
 * Cells this boot carried to the places an updated image keeps them: 0 when
 * the layout was the recorded one, -1 when the move's write did not finish.
 */
static int
persist_moved_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%d\n", tiku_persist_moved());
}

/**
 * @brief Read handler for /sys/persist/manifest.
 *
 * Where this image keeps each cell: one line per cell, its key in hex, then
 * the gate's and the value's offsets into the durable image and the value's
 * size.  A tool compares it with a new image's before flashing, to say which
 * values the update will carry.  "none" where the image keeps no record.
 */
static int
persist_manifest_read(char *buf, size_t max)
{
    const tiku_persist_manifest_t *m = tiku_persist_manifest();
    size_t n = 0;
    uint16_t i;

    if (m == NULL) {
        return snprintf(buf, max, "none\n");
    }
    for (i = 0; i < m->count && n < max; i++) {
        int w = snprintf(buf + n, max - n, "%08lx %u %u %u\n",
                         (unsigned long)m->at[i].key,
                         (unsigned)m->at[i].gate, (unsigned)m->at[i].data,
                         (unsigned)m->at[i].size);
        if (w < 0 || (size_t)w >= max - n) {
            break;                      /* whole lines only */
        }
        n += (size_t)w;
    }
    return (int)n;
}

/*---------------------------------------------------------------------------*/
/* NODE TABLE                                                                */
/*---------------------------------------------------------------------------*/

/*
 * /sys/persist directory table, exported so tiku_vfs_tree_sys.c can attach it
 * as the "persist" directory; the entry count travels as
 * TIKU_VFS_TREE_PERSIST_NCHILD (asserted below).  Every node is read-only --
 * the counters are facts about this boot, not knobs.
 */
const tiku_vfs_node_t tiku_vfs_tree_persist_children[] = {
    { "cells",  TIKU_VFS_FILE, persist_cells_read,  NULL, NULL, 0 },
    { "primed", TIKU_VFS_FILE, persist_primed_read, NULL, NULL, 0 },
    { "moved",  TIKU_VFS_FILE, persist_moved_read,  NULL, NULL, 0 },
    { "manifest", TIKU_VFS_FILE, persist_manifest_read, NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_persist_children) /
               sizeof(tiku_vfs_tree_persist_children[0])
               == TIKU_VFS_TREE_PERSIST_NCHILD,
               "TIKU_VFS_TREE_PERSIST_NCHILD out of sync");
