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
#define PROC_FILES_PER_PID  7

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

/** File nodes for each per-pid directory */
static tiku_vfs_node_t pid_files[TIKU_PROCESS_MAX][PROC_FILES_PER_PID];

/** Per-pid directory nodes + 1 for "count" */
static tiku_vfs_node_t proc_children[TIKU_PROCESS_MAX + 1];

/** Directory names: "0", "1", ... "7" */
static const char * const pid_names[] = {
    "0", "1", "2", "3", "4", "5", "6", "7"
};

/** The top-level /proc node */
static tiku_vfs_node_t proc_root;

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

/** Generate all reader functions for one pid slot */
#define PROC_READERS(idx)                                                   \
    PROC_READ_NAME(idx)                                                     \
    PROC_READ_STATE(idx)                                                    \
    PROC_READ_PID(idx)                                                      \
    PROC_READ_SRAM(idx)                                                     \
    PROC_READ_FRAM(idx)                                                     \
    PROC_READ_UPTIME(idx)                                                   \
    PROC_READ_WAKE(idx)

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
} proc_readers_t;

#define READERS_ENTRY(idx)  {                                               \
    proc_read_name_##idx,                                                   \
    proc_read_state_##idx,                                                  \
    proc_read_pid_##idx,                                                    \
    proc_read_sram_##idx,                                                   \
    proc_read_fram_##idx,                                                   \
    proc_read_uptime_##idx,                                                 \
    proc_read_wake_##idx                                                    \
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
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

const tiku_vfs_node_t *tiku_proc_vfs_get(void)
{
    uint8_t i;
    uint8_t child_idx = 0;

    /* First child: /proc/count */
    proc_children[child_idx++] = (tiku_vfs_node_t){
        "count", TIKU_VFS_FILE, proc_read_count, NULL, NULL, 0
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

uint8_t tiku_proc_vfs_child_count(void)
{
    /* 1 for "count" + one per registered process */
    return 1 + tiku_process_count();
}
