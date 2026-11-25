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
#include <stdlib.h>
#include <string.h>

#if (TIKU_PEER_ENABLE + 0)

/** @brief Most peers the tree shows; a read handler set exists for each. */
#define PEER_VFS_PEERS  2u
/** @brief Files in a peer's directory. */
#define PEER_VFS_FILES  5u

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

/**
 * @brief Switch peer @p i: "on" starts it, "off" stops it, "reset" resets it.
 * @return TIKU_VFS_OK; TIKU_VFS_EINVAL for another word; TIKU_VFS_ENOTSUP
 *         where this side has no such control of it; TIKU_VFS_EIO when its
 *         driver refuses
 */
static int
peer_state_write(uint8_t i, const char *buf, size_t len)
{
    int rc;

    while (len != 0u && (buf[len - 1u] == '\n' || buf[len - 1u] == ' ')) {
        len--;
    }
    if (len == 2u && memcmp(buf, "on", 2u) == 0) {
        rc = tiku_peer_start(i);
    } else if (len == 3u && memcmp(buf, "off", 3u) == 0) {
        rc = tiku_peer_stop(i);
    } else if (len == 5u && memcmp(buf, "reset", 5u) == 0) {
        rc = tiku_peer_reset(i);
    } else {
        return TIKU_VFS_EINVAL;
    }
    if (rc == TIKU_PEER_OK) {
        return TIKU_VFS_OK;
    }
    return (rc == TIKU_PEER_ERR_DRIVER) ? TIKU_VFS_EIO : TIKU_VFS_ENOTSUP;
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

/** @brief Render the link speed of peer @p i in bits a second, then the
 *         speed asked for when it differs. */
static int
peer_speed_read(uint8_t i, char *buf, size_t max)
{
    tiku_peer_status_t s;
    uint32_t want;

    if (tiku_peer_status(i, &s) != TIKU_PEER_OK) {
        return -1;
    }
    want = tiku_peer_speed_wanted(i);
    if (want != s.speed) {
        return snprintf(buf, max, "%lu asked %lu\n", (unsigned long)s.speed,
                        (unsigned long)want);
    }
    return snprintf(buf, max, "%lu\n", (unsigned long)s.speed);
}

/** @brief Ask for the link speed of peer @p i: a number of bits a second. */
static int
peer_speed_write(uint8_t i, const char *buf, size_t len)
{
    char text[12];
    char *end;
    unsigned long v;

    if (len == 0u || len >= sizeof(text)) {
        return TIKU_VFS_EINVAL;
    }
    memcpy(text, buf, len);
    text[len] = '\0';
    if (text[len - 1u] == '\n') {
        text[len - 1u] = '\0';
    }
    v = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || v == 0UL) {
        return TIKU_VFS_EINVAL;
    }
    return (tiku_peer_set_speed(i, (uint32_t)v) == TIKU_PEER_OK)
           ? TIKU_VFS_OK : TIKU_VFS_ERANGE;
}

/* A handler set per peer index: a VFS read handler takes no context. */
#define PEER_VFS_FUNCS(N)                                                      \
static int state_##N(char *b, size_t m)    { return peer_state_read(N, b, m); }    \
static int state_w_##N(const char *b, size_t l) { return peer_state_write(N, b, l); } \
static int role_##N(char *b, size_t m)     { return peer_role_read(N, b, m); }     \
static int carrier_##N(char *b, size_t m)  { return peer_carrier_read(N, b, m); }  \
static int restarts_##N(char *b, size_t m) { return peer_restarts_read(N, b, m); } \
static int speed_##N(char *b, size_t m)    { return peer_speed_read(N, b, m); }    \
static int speed_w_##N(const char *b, size_t l) { return peer_speed_write(N, b, l); }

PEER_VFS_FUNCS(0)
PEER_VFS_FUNCS(1)

#define PEER_VFS_ROW(N)                                                        \
    { { "state",    TIKU_VFS_FILE, state_##N, state_w_##N, NULL, 0, NULL, NULL,   \
        TIKU_VFS_CAP_SYS },                                                    \
      { "role",     TIKU_VFS_FILE, role_##N,     NULL, NULL, 0 },               \
      { "carrier",  TIKU_VFS_FILE, carrier_##N,  NULL, NULL, 0 },               \
      { "restarts", TIKU_VFS_FILE, restarts_##N, NULL, NULL, 0 },              \
      { "speed",    TIKU_VFS_FILE, speed_##N, speed_w_##N, NULL, 0, NULL, NULL,   \
        TIKU_VFS_CAP_SYS } }

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
