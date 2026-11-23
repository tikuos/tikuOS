/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_vfs.c - the board's peers under /sys/peer.
 *
 * /sys/peer/<name>/state reads "off", "starting", "ready" or "lost <reason>";
 * role reads "supervisor" or "worker", this side's part; carrier and
 * restarts read the peer's.  Every read asks the peer's driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_peer_vfs.h"
#include <interfaces/peer/tiku_peer.h>
#include <stdio.h>

#if (TIKU_PEER_ENABLE + 0)

/** @brief Most peers the tree shows; a read handler set exists for each. */
#define PEER_VFS_PEERS  2u
/** @brief Files in a peer's directory. */
#define PEER_VFS_FILES  4u

/** @brief Render the state of peer @p i. */
static int
peer_state_read(uint8_t i, char *buf, size_t max)
{
    static const char *const state[] = { "off", "starting", "ready", "lost" };
    static const char *const why[] = { "", " timeout", " fault", " hang",
                                       " carrier" };
    tiku_peer_status_t s;

    if (tiku_peer_status(i, &s) != TIKU_PEER_OK) {
        return -1;
    }
    return snprintf(buf, max, "%s%s\n", state[s.state & 3u],
                    (s.state == TIKU_PEER_LOST && s.why < 5u) ? why[s.why] : "");
}

/** @brief Render this side's role toward peer @p i. */
static int
peer_role_read(uint8_t i, char *buf, size_t max)
{
    const tiku_peer_t *p = tiku_peer_get(i);

    if (p == (const tiku_peer_t *)0) {
        return -1;
    }
    return snprintf(buf, max, "%s\n",
                    (p->flags & TIKU_PEER_F_SUPERVISOR) ? "supervisor" : "worker");
}

/** @brief Render the carrier of peer @p i. */
static int
peer_carrier_read(uint8_t i, char *buf, size_t max)
{
    const tiku_peer_t *p = tiku_peer_get(i);

    if (p == (const tiku_peer_t *)0) {
        return -1;
    }
    return snprintf(buf, max, "%s\n", p->carrier);
}

/** @brief Render how often peer @p i came back as a new instance. */
static int
peer_restarts_read(uint8_t i, char *buf, size_t max)
{
    tiku_peer_status_t s;

    if (tiku_peer_status(i, &s) != TIKU_PEER_OK) {
        return -1;
    }
    return snprintf(buf, max, "%u\n", (unsigned)s.restarts);
}

/* A handler set per peer index: a VFS read handler takes no context. */
#define PEER_VFS_FUNCS(N)                                                      \
static int state_##N(char *b, size_t m)    { return peer_state_read(N, b, m); }    \
static int role_##N(char *b, size_t m)     { return peer_role_read(N, b, m); }     \
static int carrier_##N(char *b, size_t m)  { return peer_carrier_read(N, b, m); }  \
static int restarts_##N(char *b, size_t m) { return peer_restarts_read(N, b, m); }

PEER_VFS_FUNCS(0)
PEER_VFS_FUNCS(1)

#define PEER_VFS_ROW(N)                                                        \
    { { "state",    TIKU_VFS_FILE, state_##N,    NULL, NULL, 0 },               \
      { "role",     TIKU_VFS_FILE, role_##N,     NULL, NULL, 0 },               \
      { "carrier",  TIKU_VFS_FILE, carrier_##N,  NULL, NULL, 0 },               \
      { "restarts", TIKU_VFS_FILE, restarts_##N, NULL, NULL, 0 } }

static const tiku_vfs_node_t peer_files[PEER_VFS_PEERS][PEER_VFS_FILES] = {
    PEER_VFS_ROW(0), PEER_VFS_ROW(1)
};

static tiku_vfs_node_t peer_dirs[PEER_VFS_PEERS];
static tiku_vfs_node_t peer_root;

const tiku_vfs_node_t *
tiku_peer_vfs_get(void)
{
    uint8_t i, n = tiku_peer_count;

    if (peer_root.name == (const char *)0) {
        if (n > PEER_VFS_PEERS) {
            n = PEER_VFS_PEERS;
        }
        for (i = 0; i < n; i++) {
            peer_dirs[i].name = tiku_peer_table[i]->name;
            peer_dirs[i].type = TIKU_VFS_DIR;
            peer_dirs[i].children = peer_files[i];
            peer_dirs[i].child_count = PEER_VFS_FILES;
        }
        peer_root.type = TIKU_VFS_DIR;
        peer_root.children = peer_dirs;
        peer_root.child_count = n;
        peer_root.name = "peer";
    }
    return &peer_root;
}

#endif /* TIKU_PEER_ENABLE */
