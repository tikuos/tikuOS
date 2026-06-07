/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_persist.c - /sys/persist VFS nodes
 *
 * Two read-only counters that make the persist-cell layer
 * observable from the namespace it serves:
 *
 *   /sys/persist/cells   cells validated by tiku_persist_cell_init()
 *                        this boot (the persistent footprint of the
 *                        running image)
 *   /sys/persist/primed  of those, how many had to be primed to
 *                        defaults because their gate did not
 *                        validate
 *
 * `primed` is the diagnostic: 0 on every boot of an established
 * device.  Non-zero exactly once after first flash, after a reflash
 * whose layout moved the `.persistent` cells, or after an NVM wipe
 * — and on any other boot it is a forensic signal that NVM content
 * was lost or corrupted in the field (`cat /sys/persist/primed` is
 * the first question to ask a device whose counters look wrong).
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

#include "tiku_vfs_tree_persist.h"
#include "tiku.h"
#include <kernel/memory/tiku_mem.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /sys/persist/cells, /sys/persist/primed                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/persist/cells.
 *
 * Renders the number of persist cells validated this boot as a
 * decimal line ("4\n" with the stock tree: boot counter, lifetime
 * accumulator, device name, RTC epoch).  A per-boot statistic from
 * tiku_persist_cell_count() — it counts cell_init() calls, not a
 * registry, so cells whose init has not run yet do not appear.
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

/*---------------------------------------------------------------------------*/
/* NODE TABLE                                                                */
/*---------------------------------------------------------------------------*/

/**
 * /sys/persist directory table.
 *
 * Exported so tiku_vfs_tree_sys.c can attach it as the "persist"
 * directory; the entry count travels as
 * TIKU_VFS_TREE_PERSIST_NCHILD (asserted below).  Both nodes are
 * read-only — the counters are facts about this boot, not knobs.
 */
const tiku_vfs_node_t tiku_vfs_tree_persist_children[] = {
    { "cells",  TIKU_VFS_FILE, persist_cells_read,  NULL, NULL, 0 },
    { "primed", TIKU_VFS_FILE, persist_primed_read, NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_persist_children) /
               sizeof(tiku_vfs_tree_persist_children[0])
               == TIKU_VFS_TREE_PERSIST_NCHILD,
               "TIKU_VFS_TREE_PERSIST_NCHILD out of sync");
