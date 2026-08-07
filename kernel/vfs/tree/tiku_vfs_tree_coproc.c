/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_coproc.c - /sys/coproc, over interfaces/coproc alone.
 *
 * Portable by construction: every handler goes through tiku_coproc_*, so the
 * tree reads the same on any platform that carries a backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include <stdio.h>

#include <kernel/vfs/tiku_vfs.h>
#include <interfaces/coproc/tiku_coproc.h>

#include "tiku_vfs_tree_coproc.h"

#if (TIKU_HAS_COPROC + 0)

static int coproc_state_read(char *buf, size_t max)
{
    static const char *const names[] = {
        "absent", "stopped", "started", "running", "faulted"
    };
    tiku_coproc_state_t st = tiku_coproc_state();

    if ((unsigned)st > (unsigned)TIKU_COPROC_FAULTED) {
        st = TIKU_COPROC_ABSENT;
    }
    return snprintf(buf, max, "%s\n", names[st]);
}

static int coproc_heartbeat_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)tiku_coproc_heartbeat());
}

static int coproc_image_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu flags=%lx cap=%lu\n",
                    (unsigned long)tiku_coproc_image_size(),
                    (unsigned long)tiku_coproc_flags(),
                    (unsigned long)TIKU_COPROC_MSG_CAP);
}

static int coproc_run_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%d\n",
                    tiku_coproc_state() == TIKU_COPROC_RUNNING ? 1 : 0);
}

static int coproc_run_write(const char *buf, size_t len)
{
    if (len >= 1u && buf[0] == '1') {
        return (tiku_coproc_start() == TIKU_COPROC_OK) ? 0 : TIKU_VFS_EINVAL;
    }
    if (len >= 1u && buf[0] == '0') {
        return (tiku_coproc_stop() == TIKU_COPROC_OK) ? 0 : TIKU_VFS_EINVAL;
    }
    return TIKU_VFS_EINVAL;
}

/*
 * Echo surface over the mailbox: writing sends the bytes, reading returns
 * "<reply_seq> <last reply>".  A seq that advances after a write proves the
 * cross-core path; the bench suite asserts it.
 */
static int coproc_echo_read(char *buf, size_t max)
{
    char body[TIKU_COPROC_MSG_CAP + 1u];
    uint32_t n;

    (void)tiku_coproc_poll();
    n = tiku_coproc_reply(body, sizeof(body) - 1u);
    body[n] = '\0';
    return snprintf(buf, max, "%lu %s\n",
                    (unsigned long)tiku_coproc_reply_seq(), body);
}

static int coproc_echo_write(const char *buf, size_t len)
{
    return (tiku_coproc_send(buf, (uint32_t)len) == TIKU_COPROC_OK)
               ? 0 : TIKU_VFS_EINVAL;
}

const tiku_vfs_node_t tiku_vfs_tree_coproc_children[] = {
    { "state",     TIKU_VFS_FILE, coproc_state_read,     NULL, NULL, 0 },
    { "heartbeat", TIKU_VFS_FILE, coproc_heartbeat_read, NULL, NULL, 0 },
    { "image",     TIKU_VFS_FILE, coproc_image_read,     NULL, NULL, 0 },
    /* Launch is a one-way door on every backend so far; gate it the way the
     * watchdog gates its controls. */
    { "run",       TIKU_VFS_FILE, coproc_run_read,  coproc_run_write,
      NULL, 0, NULL, NULL, TIKU_VFS_CAP_SYS },
    { "echo",      TIKU_VFS_FILE, coproc_echo_read, coproc_echo_write,
      NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_coproc_children) /
               sizeof(tiku_vfs_tree_coproc_children[0]) ==
               TIKU_VFS_TREE_COPROC_NCHILD,
               "TIKU_VFS_TREE_COPROC_NCHILD is out of step with the table");

#endif /* TIKU_HAS_COPROC */
