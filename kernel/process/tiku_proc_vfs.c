/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_proc_vfs.c - VFS /proc/ virtual node implementation
 *
 * Registers virtual VFS nodes under /proc/ that read from the
 * process registry. Each registered process gets a directory
 * /proc/<pid>/ with files for name, state, pid, sram_used,
 * fram_used, uptime, and wake_count.
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

#include "tiku_proc_vfs.h"
#include "tiku_process.h"
#include <kernel/timers/tiku_clock.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/** Number of file nodes per process directory */
#define PROC_FILES_PER_PID  8

/** Maximum catalog entries that get VFS nodes */
#define PROC_CATALOG_VFS_MAX  TIKU_PROCESS_CATALOG_MAX

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/*
 * Static storage for the VFS tree.  The tree is rebuilt by
 * tiku_proc_vfs_get() to reflect the current registry state.
 *
 * Layout:
 *   proc_root ("proc", DIR)
 *     ├── "count" (FILE)
 *     ├── "0"     (DIR)  →  pid_files[0][0..6]
 *     ├── "1"     (DIR)  →  pid_files[1][0..6]
 *     └── ...
 */

/** File nodes for each per-pid directory (FRAM) */
static tiku_vfs_node_t __attribute__((section(".persistent")))
    pid_files[TIKU_PROCESS_MAX][PROC_FILES_PER_PID];

/** Per-pid directory nodes + count + queue dir + catalog dir (FRAM) */
static tiku_vfs_node_t __attribute__((section(".persistent")))
    proc_children[TIKU_PROCESS_MAX + 3];

/** Directory names: "0", "1", ... "7" */
static const char * const pid_names[] = {
    "0", "1", "2", "3", "4", "5", "6", "7"
};

/** The top-level /proc node (FRAM) */
static tiku_vfs_node_t __attribute__((section(".persistent")))
    proc_root;

/*---------------------------------------------------------------------------*/
/* READER HELPERS                                                            */
/*---------------------------------------------------------------------------*/

/*
 * Each read handler needs to know which pid it serves.  Since the
 * VFS read signature is int(char *, size_t) with no context pointer,
 * we use a set of per-pid handler functions generated via a macro.
 * This is the embedded-idiomatic approach: zero runtime overhead,
 * statically resolved at build time.
 */

#define PROC_READ_NAME(idx)                                                 \
    static int proc_read_name_##idx(char *buf, size_t max)                  \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "(none)\n"); }           \
        return snprintf(buf, max, "%s\n", p->name ? p->name : "(null)");   \
    }

#define PROC_READ_STATE(idx)                                                \
    static int proc_read_state_##idx(char *buf, size_t max)                 \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "(none)\n"); }           \
        return snprintf(buf, max, "%s\n",                                   \
                        tiku_process_state_str(p->state));                  \
    }

#define PROC_READ_PID(idx)                                                  \
    static int proc_read_pid_##idx(char *buf, size_t max)                   \
    {                                                                       \
        return snprintf(buf, max, "%d\n", idx);                             \
    }

#define PROC_READ_SRAM(idx)                                                 \
    static int proc_read_sram_##idx(char *buf, size_t max)                  \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        return snprintf(buf, max, "%u\n", p->sram_used);                   \
    }

#define PROC_READ_FRAM(idx)                                                 \
    static int proc_read_fram_##idx(char *buf, size_t max)                  \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        return snprintf(buf, max, "%u\n", p->fram_used);                   \
    }

#define PROC_READ_UPTIME(idx)                                               \
    static int proc_read_uptime_##idx(char *buf, size_t max)                \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        unsigned long secs;                                                 \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        secs = (unsigned long)(tiku_clock_time() - p->start_time) /         \
               TIKU_CLOCK_SECOND;                                           \
        return snprintf(buf, max, "%lu\n", secs);                           \
    }

#define PROC_READ_WAKE(idx)                                                 \
    static int proc_read_wake_##idx(char *buf, size_t max)                  \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        return snprintf(buf, max, "%u\n", p->wake_count);                  \
    }

#define PROC_READ_EVENTS(idx)                                               \
    static int proc_read_events_##idx(char *buf, size_t max)                \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        uint8_t len, i, cnt = 0;                                            \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        len = tiku_process_queue_length();                                  \
        for (i = 0; i < len; i++) {                                         \
            struct tiku_process *tgt = NULL;                                 \
            tiku_process_queue_peek(i, NULL, &tgt);                         \
            if (tgt == p) { cnt++; }                                        \
        }                                                                   \
        return snprintf(buf, max, "%u\n", cnt);                             \
    }

/** Generate all reader functions for one pid slot */
#define PROC_READERS(idx)                                                   \
    PROC_READ_NAME(idx)                                                     \
    PROC_READ_STATE(idx)                                                    \
    PROC_READ_PID(idx)                                                      \
    PROC_READ_SRAM(idx)                                                     \
    PROC_READ_FRAM(idx)                                                     \
    PROC_READ_UPTIME(idx)                                                   \
    PROC_READ_WAKE(idx)                                                     \
    PROC_READ_EVENTS(idx)

/* Generate reader functions for all 8 pid slots */
PROC_READERS(0)
PROC_READERS(1)
PROC_READERS(2)
PROC_READERS(3)
PROC_READERS(4)
PROC_READERS(5)
PROC_READERS(6)
PROC_READERS(7)

/** Lookup table for per-pid reader functions */
typedef struct {
    tiku_vfs_read_fn name;
    tiku_vfs_read_fn state;
    tiku_vfs_read_fn pid;
    tiku_vfs_read_fn sram;
    tiku_vfs_read_fn fram;
    tiku_vfs_read_fn uptime;
    tiku_vfs_read_fn wake;
    tiku_vfs_read_fn events;
} proc_readers_t;

#define READERS_ENTRY(idx)  {                                               \
    proc_read_name_##idx,                                                   \
    proc_read_state_##idx,                                                  \
    proc_read_pid_##idx,                                                    \
    proc_read_sram_##idx,                                                   \
    proc_read_fram_##idx,                                                   \
    proc_read_uptime_##idx,                                                 \
    proc_read_wake_##idx,                                                   \
    proc_read_events_##idx                                                  \
}

static const proc_readers_t readers[TIKU_PROCESS_MAX] = {
    READERS_ENTRY(0), READERS_ENTRY(1), READERS_ENTRY(2), READERS_ENTRY(3),
    READERS_ENTRY(4), READERS_ENTRY(5), READERS_ENTRY(6), READERS_ENTRY(7)
};

/*---------------------------------------------------------------------------*/
/* /proc/count READER                                                        */
/*---------------------------------------------------------------------------*/

static int proc_read_count(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_process_count());
}

/*---------------------------------------------------------------------------*/
/* /proc/queue READERS                                                       */
/*---------------------------------------------------------------------------*/

static int proc_read_queue_length(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_process_queue_length());
}

static int proc_read_queue_space(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_process_queue_space());
}

static const tiku_vfs_node_t proc_queue_children[] = {
    { "length", TIKU_VFS_FILE, proc_read_queue_length, NULL, NULL, 0 },
    { "space",  TIKU_VFS_FILE, proc_read_queue_space,  NULL, NULL, 0 },
};

/*---------------------------------------------------------------------------*/
/* /proc/catalog READERS                                                     */
/*---------------------------------------------------------------------------*/

static int proc_read_catalog_count(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_process_catalog_count());
}

#define PROC_READ_CATALOG_NAME(idx)                                         \
    static int proc_read_catname_##idx(char *buf, size_t max)               \
    {                                                                       \
        const tiku_process_catalog_entry_t *e =                             \
            tiku_process_catalog_get(idx);                                  \
        if (e == NULL) { return snprintf(buf, max, "(none)\n"); }          \
        return snprintf(buf, max, "%s\n", e->name ? e->name : "(null)");   \
    }

PROC_READ_CATALOG_NAME(0)
PROC_READ_CATALOG_NAME(1)
PROC_READ_CATALOG_NAME(2)
PROC_READ_CATALOG_NAME(3)
PROC_READ_CATALOG_NAME(4)
PROC_READ_CATALOG_NAME(5)
PROC_READ_CATALOG_NAME(6)
PROC_READ_CATALOG_NAME(7)

static const tiku_vfs_read_fn catalog_name_readers[PROC_CATALOG_VFS_MAX] = {
    proc_read_catname_0, proc_read_catname_1,
    proc_read_catname_2, proc_read_catname_3,
    proc_read_catname_4, proc_read_catname_5,
    proc_read_catname_6, proc_read_catname_7,
};

/** Per-catalog-entry file nodes (FRAM) */
static tiku_vfs_node_t __attribute__((section(".persistent")))
    catalog_entry_files[PROC_CATALOG_VFS_MAX][1];

/** Catalog directory children: "count" + one dir per entry (FRAM) */
static tiku_vfs_node_t __attribute__((section(".persistent")))
    catalog_children[1 + PROC_CATALOG_VFS_MAX];

/** Directory names reused from pid_names[] above */

/*---------------------------------------------------------------------------*/
/* TREE BUILDER                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Fill the file nodes for a single pid directory
 */
static void build_pid_files(uint8_t idx)
{
    tiku_vfs_node_t *f = pid_files[idx];

    f[0] = (tiku_vfs_node_t){
        "name",       TIKU_VFS_FILE, readers[idx].name,   NULL, NULL, 0};
    f[1] = (tiku_vfs_node_t){
        "state",      TIKU_VFS_FILE, readers[idx].state,  NULL, NULL, 0};
    f[2] = (tiku_vfs_node_t){
        "pid",        TIKU_VFS_FILE, readers[idx].pid,    NULL, NULL, 0};
    f[3] = (tiku_vfs_node_t){
        "sram_used",  TIKU_VFS_FILE, readers[idx].sram,   NULL, NULL, 0};
    f[4] = (tiku_vfs_node_t){
        "fram_used",  TIKU_VFS_FILE, readers[idx].fram,   NULL, NULL, 0};
    f[5] = (tiku_vfs_node_t){
        "uptime",     TIKU_VFS_FILE, readers[idx].uptime, NULL, NULL, 0};
    f[6] = (tiku_vfs_node_t){
        "wake_count", TIKU_VFS_FILE, readers[idx].wake,   NULL, NULL, 0};
    f[7] = (tiku_vfs_node_t){
        "events",     TIKU_VFS_FILE, readers[idx].events, NULL, NULL, 0};
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

const tiku_vfs_node_t *tiku_proc_vfs_get(void)
{
    uint8_t i;
    uint8_t child_idx = 0;
    uint8_t cat_child_idx = 0;
    uint8_t cat_count;

    /* /proc/count */
    proc_children[child_idx++] = (tiku_vfs_node_t){
        "count", TIKU_VFS_FILE, proc_read_count, NULL, NULL, 0
    };

    /* /proc/queue/ */
    proc_children[child_idx++] = (tiku_vfs_node_t){
        "queue", TIKU_VFS_DIR, NULL, NULL, proc_queue_children, 2
    };

    /* /proc/catalog/ — build entries for each catalog slot */
    cat_count = tiku_process_catalog_count();
    catalog_children[cat_child_idx++] = (tiku_vfs_node_t){
        "count", TIKU_VFS_FILE, proc_read_catalog_count, NULL, NULL, 0
    };
    for (i = 0; i < cat_count && i < PROC_CATALOG_VFS_MAX; i++) {
        catalog_entry_files[i][0] = (tiku_vfs_node_t){
            "name", TIKU_VFS_FILE, catalog_name_readers[i], NULL, NULL, 0
        };
        catalog_children[cat_child_idx++] = (tiku_vfs_node_t){
            pid_names[i], TIKU_VFS_DIR, NULL, NULL,
            catalog_entry_files[i], 1
        };
    }
    proc_children[child_idx++] = (tiku_vfs_node_t){
        "catalog", TIKU_VFS_DIR, NULL, NULL,
        catalog_children, cat_child_idx
    };

    /* One directory per registered process */
    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        if (tiku_process_get((int8_t)i) != NULL) {
            build_pid_files(i);
            proc_children[child_idx++] = (tiku_vfs_node_t){
                pid_names[i], TIKU_VFS_DIR, NULL, NULL,
                pid_files[i], PROC_FILES_PER_PID
            };
        }
    }

    proc_root = (tiku_vfs_node_t){
        "proc", TIKU_VFS_DIR, NULL, NULL, proc_children, child_idx
    };

    return &proc_root;
}

/**
 * @brief Return the current number of children under /proc.
 *
 * The /proc directory contains three fixed nodes (count, queue,
 * catalog) plus one subdirectory per registered process.
 *
 * @return Total child count (3 + number of registered processes).
 */
uint8_t tiku_proc_vfs_child_count(void)
{
    /* count + queue + catalog + one per registered process */
    return 3 + tiku_process_count();
}
