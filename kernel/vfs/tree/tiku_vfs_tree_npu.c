/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_npu.c - /sys/npu over interfaces/npu.
 *
 * Reads describe the accelerator and the loaded model; writing to run
 * submits one inference over whatever the input buffer already holds.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_vfs_tree_npu.h"

#if (TIKU_HAS_NPU + 0)

#include <interfaces/npu/tiku_npu.h>

static int npu_state_read(char *buf, size_t max)
{
    static const char *const names[] = {
        "absent", "gated", "idle", "ready", "faulted"
    };
    tiku_npu_state_t st = tiku_npu_state();

    if ((unsigned)st > (unsigned)TIKU_NPU_FAULTED) {
        st = TIKU_NPU_ABSENT;
    }
    return snprintf(buf, max, "%s\n", names[st]);
}

static int npu_info_read(char *buf, size_t max)
{
    tiku_npu_info_t i;

    if (tiku_npu_info(&i) != TIKU_NPU_OK) {
        return snprintf(buf, max, "gated\n");
    }
    return snprintf(buf, max, "%u macs %u KB shram %lu arena %lu in %lu out\n",
                    (unsigned)i.macs, (unsigned)i.shram_kb,
                    (unsigned long)i.arena, (unsigned long)i.in_bytes,
                    (unsigned long)i.out_bytes);
}

static int npu_runs_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)tiku_npu_runs());
}

/*
 * Writing a name loads that model; writing nothing releases the accelerator
 * so the other nodes have something to describe.
 */
static int npu_model_write(const char *buf, size_t len)
{
    char name[48];

    if (len == 0u) {
        return (tiku_npu_start() == TIKU_NPU_OK) ? 0 : TIKU_VFS_EINVAL;
    }
    if (len >= sizeof name) {
        return TIKU_VFS_EINVAL;
    }
    memcpy(name, buf, len);
    name[len] = '\0';
    while (len != 0u && (name[len - 1u] == '\n' || name[len - 1u] == '\r')) {
        name[--len] = '\0';
    }
    return (tiku_npu_load(name) == TIKU_NPU_OK) ? 0 : TIKU_VFS_EINVAL;
}

static int npu_model_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n",
                    (tiku_npu_state() == TIKU_NPU_READY) ? "loaded" : "none");
}

/* Any write submits one inference; the body is not interpreted. */
static int npu_run_write(const char *buf, size_t len)
{
    (void)buf; (void)len;
    return (tiku_npu_run() == TIKU_NPU_OK) ? 0 : TIKU_VFS_EINVAL;
}

static int npu_run_read(char *buf, size_t max)
{
    return npu_runs_read(buf, max);
}

const tiku_vfs_node_t tiku_vfs_tree_npu_children[] = {
    { "state", TIKU_VFS_FILE, npu_state_read, NULL,           NULL, 0 },
    { "info",  TIKU_VFS_FILE, npu_info_read,  NULL,           NULL, 0 },
    { "model", TIKU_VFS_FILE, npu_model_read, npu_model_write, NULL, 0 },
    { "run",   TIKU_VFS_FILE, npu_run_read,   npu_run_write,  NULL, 0 },
    { "runs",  TIKU_VFS_FILE, npu_runs_read,  NULL,           NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_npu_children) /
               sizeof(tiku_vfs_tree_npu_children[0]) ==
               TIKU_VFS_TREE_NPU_NCHILD,
               "/sys/npu child count disagrees with its header");

#endif /* TIKU_HAS_NPU */
