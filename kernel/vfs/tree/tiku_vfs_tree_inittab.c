/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_inittab.c - /sys/init VFS nodes (init-table mirror)
 *
 * Mirror the FRAM-backed init table at /sys/init/<idx>/{seq,name,
 * cmd,enable}. Reading is always available; writing `enable` is the
 * single mutating path so a BASIC program (or any VFS client) can
 * enable / disable a boot entry without going through the shell.
 *
 * Add / remove of entries is intentionally NOT exposed here -- those
 * are multi-field operations that don't fit single-node writes.
 * Use the `init` shell command (or its parser API) for those.
 *
 * Slot count is fixed at TIKU_INIT_MAX_ENTRIES (default 8); empty
 * slots return placeholder text so a passive `tree /sys/init` works
 * regardless of the table's fill level.
 *
 * The whole module compiles away when TIKU_INIT_ENABLE is 0: the
 * translation unit then contains only the includes, and the /sys
 * assembly omits its "init" entry under the same flag.
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

#include "tiku_vfs_tree_inittab.h"
#include "tiku.h"

#if TIKU_INIT_ENABLE

#include <kernel/init/tiku_init.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /sys/init -- per-slot init-table entries                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/init/count.
 *
 * Renders the number of populated init-table entries as a decimal
 * line — i.e. how many of the eight slot directories below hold a
 * real entry rather than "(none)" placeholders.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
init_count_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)tiku_init_count());
}

/**
 * @brief Generate the four handlers + children table for slot N.
 *
 * Each expansion produces:
 *
 *   init_seq_N_read     boot-order sequence number, "(none)\n" if
 *                       the slot is empty
 *   init_name_N_read    entry name, empty line if the slot is empty
 *   init_cmd_N_read     shell command the entry runs at boot
 *   init_enable_N_read  "1\n" enabled / "0\n" disabled-or-empty
 *   init_enable_N_write writes through tiku_init_enable() (which
 *                       persists to FRAM); first byte '0' disables,
 *                       anything else enables; -1 on empty slot
 *   init_N_children[]   the four nodes of /sys/init/N/
 *
 * All five functions re-fetch the entry on every call: slots can
 * be added/removed at runtime by the `init` shell command, so
 * caching pointers across calls would go stale.
 */
#define INIT_VFS_FUNCS(N)                                                     \
static int init_seq_##N##_read(char *buf, size_t max)                         \
{                                                                             \
    const tiku_init_entry_t *e = tiku_init_get(N);                            \
    if (!e) return snprintf(buf, max, "(none)\n");                            \
    return snprintf(buf, max, "%u\n", (unsigned)e->seq);                      \
}                                                                             \
static int init_name_##N##_read(char *buf, size_t max)                        \
{                                                                             \
    const tiku_init_entry_t *e = tiku_init_get(N);                            \
    if (!e) return snprintf(buf, max, "\n");                                  \
    return snprintf(buf, max, "%s\n", e->name);                               \
}                                                                             \
static int init_cmd_##N##_read(char *buf, size_t max)                         \
{                                                                             \
    const tiku_init_entry_t *e = tiku_init_get(N);                            \
    if (!e) return snprintf(buf, max, "\n");                                  \
    return snprintf(buf, max, "%s\n", e->cmd);                                \
}                                                                             \
static int init_enable_##N##_read(char *buf, size_t max)                      \
{                                                                             \
    const tiku_init_entry_t *e = tiku_init_get(N);                            \
    if (!e) return snprintf(buf, max, "0\n");                                 \
    return snprintf(buf, max, "%u\n", (unsigned)e->enabled);                  \
}                                                                             \
static int init_enable_##N##_write(const char *buf, size_t len)               \
{                                                                             \
    const tiku_init_entry_t *e = tiku_init_get(N);                            \
    (void)len;                                                                \
    if (!e) return -1;                                                        \
    return tiku_init_enable(e->name, (uint8_t)(buf[0] != '0'));               \
}                                                                             \
static const tiku_vfs_node_t init_##N##_children[] = {                        \
    { "seq",    TIKU_VFS_FILE, init_seq_##N##_read,    NULL,                  \
      NULL, 0 },                                                              \
    { "name",   TIKU_VFS_FILE, init_name_##N##_read,   NULL,                  \
      NULL, 0 },                                                              \
    { "cmd",    TIKU_VFS_FILE, init_cmd_##N##_read,    NULL,                  \
      NULL, 0 },                                                              \
    { "enable", TIKU_VFS_FILE, init_enable_##N##_read,                        \
                                init_enable_##N##_write, NULL, 0 },           \
}

INIT_VFS_FUNCS(0);
INIT_VFS_FUNCS(1);
INIT_VFS_FUNCS(2);
INIT_VFS_FUNCS(3);
INIT_VFS_FUNCS(4);
INIT_VFS_FUNCS(5);
INIT_VFS_FUNCS(6);
INIT_VFS_FUNCS(7);
#if TIKU_INIT_MAX_ENTRIES != 8
#  error "INIT_VFS_FUNCS expansions assume TIKU_INIT_MAX_ENTRIES == 8"
#endif

/*---------------------------------------------------------------------------*/
/* NODE TABLE                                                                */
/*---------------------------------------------------------------------------*/

/**
 * /sys/init directory table: the count file plus one directory per
 * slot.  Slot directories exist even for empty slots (their files
 * render placeholders) so the tree shape is stable regardless of
 * how full the init table is.
 *
 * Exported so tiku_vfs_tree_sys.c can attach it (guarded by
 * TIKU_INIT_ENABLE there); count travels as
 * TIKU_VFS_TREE_INITTAB_NCHILD (asserted below).
 */
const tiku_vfs_node_t tiku_vfs_tree_inittab_children[] = {
    { "count", TIKU_VFS_FILE, init_count_read, NULL,  NULL, 0 },
    { "0", TIKU_VFS_DIR, NULL, NULL, init_0_children, 4 },
    { "1", TIKU_VFS_DIR, NULL, NULL, init_1_children, 4 },
    { "2", TIKU_VFS_DIR, NULL, NULL, init_2_children, 4 },
    { "3", TIKU_VFS_DIR, NULL, NULL, init_3_children, 4 },
    { "4", TIKU_VFS_DIR, NULL, NULL, init_4_children, 4 },
    { "5", TIKU_VFS_DIR, NULL, NULL, init_5_children, 4 },
    { "6", TIKU_VFS_DIR, NULL, NULL, init_6_children, 4 },
    { "7", TIKU_VFS_DIR, NULL, NULL, init_7_children, 4 },
};

_Static_assert(sizeof(tiku_vfs_tree_inittab_children) /
               sizeof(tiku_vfs_tree_inittab_children[0])
               == TIKU_VFS_TREE_INITTAB_NCHILD,
               "TIKU_VFS_TREE_INITTAB_NCHILD out of sync");

#endif /* TIKU_INIT_ENABLE */
