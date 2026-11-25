/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_mount.c - /mnt/<name>: a peer's file tree as a directory here.
 *
 * /mnt is a dynamic directory with an entry for each peer that is ready
 * and mounted.  Its operations turn a VFS call into requests and wait for
 * the answers, so they return what the same call returns on the peer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_peer_mount.h"
#include <interfaces/peer/tiku_peer.h>
#include <limits.h>
#include <string.h>

#if (TIKU_PEER_ENABLE + 0)

/** @brief Read a 32-bit little-endian value. */
static uint32_t
get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/**
 * @brief Send one request and take its answer.
 *
 * @param name  Path under the mount, without a leading slash; a trailing
 *              slash is dropped.  NULL sends no path.
 * @param out   Receives the answer, TIKU_PEER_MOUNT_MSG bytes of room
 * @param used  Receives the bytes after the answer's header
 * @return The peer's status, or tiku_peer_mount_call()'s when it is
 *         negative; TIKU_VFS_EINVAL for a path or data too long for one
 *         request; TIKU_VFS_EIO for an answer that is not one
 */
static int
ask(uint8_t peer, uint8_t op, const char *name, uint32_t arg,
    const void *data, uint8_t len, uint8_t *out, uint8_t *used)
{
    uint8_t request[TIKU_PEER_MOUNT_MSG];
    uint8_t plen = 0u, got = 0u;
    int rc;

    if (name != (const char *)0) {
        size_t n = strlen(name);

        if (n != 0u && name[n - 1u] == '/') {
            n--;
        }
        if (n + 1u > TIKU_PEER_MOUNT_DATA) {
            return TIKU_VFS_EINVAL;
        }
        request[TIKU_PEER_MOUNT_HEAD] = (uint8_t)'/';
        memcpy(request + TIKU_PEER_MOUNT_HEAD + 1u, name, n);
        plen = (uint8_t)(n + 1u);
    }
    if ((uint16_t)plen + len > TIKU_PEER_MOUNT_DATA) {
        return TIKU_VFS_EINVAL;
    }
    request[0] = TIKU_PEER_MOUNT_REQUEST;
    request[1] = 0u;
    request[2] = op;
    request[3] = plen;
    request[4] = (uint8_t)arg;
    request[5] = (uint8_t)(arg >> 8);
    request[6] = (uint8_t)(arg >> 16);
    request[7] = (uint8_t)(arg >> 24);
    if (len != 0u) {
        memcpy(request + TIKU_PEER_MOUNT_HEAD + plen, data, len);
    }
    rc = tiku_peer_mount_call(peer, request,
                              (uint8_t)(TIKU_PEER_MOUNT_HEAD + plen + len),
                              out, &got);
    if (rc < 0) {
        return rc;
    }
    if (got < TIKU_PEER_MOUNT_HEAD || got > TIKU_PEER_MOUNT_MSG ||
        out[0] != TIKU_PEER_MOUNT_ANSWER || (int8_t)out[2] > 0) {
        return TIKU_VFS_EIO;
    }
    *used = (uint8_t)(got - TIKU_PEER_MOUNT_HEAD);
    return (int)(int8_t)out[2];
}

/*---------------------------------------------------------------------------*/
/* THE OPERATIONS                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Copy up to @p max bytes of a file from @p offset, part by part.
 * @return Bytes copied, 0 at the end; a negative status when no byte came.
 * @note A failure after the first part ends the copy with what came: the
 *       peer serves a file that is not streamed up to its read buffer.
 */
static int
mount_read_at(uint8_t peer, const char *name, uint32_t offset, char *buf,
              size_t max, uint32_t *total)
{
    uint8_t answer[TIKU_PEER_MOUNT_MSG];
    size_t copied = 0u;
    uint32_t whole = 0u;
    uint8_t n = 0u;

    if (max > (size_t)INT_MAX) {
        max = (size_t)INT_MAX;
    }
    do {
        int rc = ask(peer, TIKU_PEER_MOUNT_READ, name,
                     offset + (uint32_t)copied, (const void *)0, 0u,
                     answer, &n);

        if (rc < 0) {
            if (copied == 0u) {
                return rc;
            }
            break;
        }
        whole = get32(answer + 4);
        if ((size_t)n > max - copied) {
            n = (uint8_t)(max - copied);
        }
        memcpy(buf + copied, answer + TIKU_PEER_MOUNT_HEAD, n);
        copied += n;
    } while (n != 0u && copied < max && offset + copied < whole);
    if (total != (uint32_t *)0) {
        *total = whole;
    }
    return (int)copied;
}

/** @brief Read a file from its start; its whole length, or what came of it
 *         when the peer stopped before @p max bytes and before its end. */
static int
mount_read(uint8_t peer, const char *name, char *buf, size_t max)
{
    uint32_t total = 0u;
    int n = mount_read_at(peer, name, 0u, buf, max, &total);

    if (n < 0) {
        return n;
    }
    if ((size_t)n < max && (uint32_t)n < total) {
        return n;
    }
    return (total > (uint32_t)INT_MAX) ? INT_MAX : (int)total;
}

/** @brief Write a value: one WRITE, then MORE requests for what is left. */
static int
mount_write(uint8_t peer, const char *name, const char *buf, size_t len)
{
    uint8_t answer[TIKU_PEER_MOUNT_MSG];
    size_t plen = strlen(name) + 1u, sent;
    uint8_t n, used;
    int rc;

    if (plen > TIKU_PEER_MOUNT_DATA) {
        return TIKU_VFS_EINVAL;
    }
    if (len > 0xFFFFu) {
        return TIKU_VFS_E2BIG;
    }
    n = (uint8_t)((len < TIKU_PEER_MOUNT_DATA - plen)
                  ? len : TIKU_PEER_MOUNT_DATA - plen);
    rc = ask(peer, TIKU_PEER_MOUNT_WRITE, name, (uint32_t)len, buf, n,
             answer, &used);
    for (sent = n; rc == TIKU_VFS_OK && sent < len; sent += n) {
        if ((answer[3] & TIKU_PEER_MOUNT_F_MORE) == 0u) {
            return TIKU_VFS_EIO;
        }
        n = (uint8_t)((len - sent < TIKU_PEER_MOUNT_DATA)
                      ? len - sent : TIKU_PEER_MOUNT_DATA);
        rc = ask(peer, TIKU_PEER_MOUNT_MORE, (const char *)0, (uint32_t)sent,
                 buf + sent, n, answer, &used);
    }
    if (rc == TIKU_VFS_OK && (answer[3] & TIKU_PEER_MOUNT_F_MORE) != 0u) {
        return TIKU_VFS_EIO;
    }
    return rc;
}

/** @brief Delete a file. */
static int
mount_unlink(uint8_t peer, const char *name)
{
    uint8_t answer[TIKU_PEER_MOUNT_MSG];
    uint8_t used;

    return ask(peer, TIKU_PEER_MOUNT_UNLINK, name, 0u, (const void *)0, 0u,
               answer, &used);
}

/** @brief 1 for a directory, 0 for a file, or a negative status. */
static int
mount_is_dir(uint8_t peer, const char *name)
{
    uint8_t answer[TIKU_PEER_MOUNT_MSG];
    uint8_t used;
    int rc = ask(peer, TIKU_PEER_MOUNT_STAT, name, 0u, (const void *)0, 0u,
                 answer, &used);

    if (rc < 0) {
        return rc;
    }
    return (answer[3] & TIKU_PEER_MOUNT_F_DIR) ? 1 : 0;
}

/**
 * @brief Call @p cb for each child of the directory @p prefix names.
 * @return TIKU_VFS_OK, the peer's status, or TIKU_VFS_EIO for names that
 *         do not end inside their answer or an answer that passes nothing
 * @note @p cb may itself ask the peer: each request keeps its answer in its
 *       caller's frame.
 */
static int
mount_list(uint8_t peer, const char *prefix, tiku_vfs_dyn_list_cb cb,
           void *ctx)
{
    uint8_t answer[TIKU_PEER_MOUNT_MSG + 1u];
    char *names = (char *)answer + TIKU_PEER_MOUNT_HEAD;
    uint32_t cursor = 0u;

    for (;;) {
        uint32_t passed;
        uint8_t used = 0u, at = 0u, more;
        int rc = ask(peer, TIKU_PEER_MOUNT_LIST, prefix, cursor,
                     (const void *)0, 0u, answer, &used);

        if (rc < 0 && used == 0u) {
            return rc;
        }
        passed = get32(answer + 4);
        more = (uint8_t)(answer[3] & TIKU_PEER_MOUNT_F_MORE);
        names[used] = '\0';
        while (at < used) {
            size_t n = strlen(names + at);

            if ((size_t)at + n >= used) {
                return TIKU_VFS_EIO;
            }
            if (n != 0u) {
                cb(names + at, ctx);
            }
            at = (uint8_t)(at + n + 1u);
        }
        if (rc < 0) {
            return rc;
        }
        if (more == 0u) {
            return TIKU_VFS_OK;
        }
        if (passed == 0u) {
            return TIKU_VFS_EIO;
        }
        cursor += passed;
    }
}

/*---------------------------------------------------------------------------*/
/* THE DIRECTORY                                                             */
/*---------------------------------------------------------------------------*/

/** @brief A bit for each peer taken out of /mnt by tiku_peer_mount_set(). */
static uint8_t mount_out;

int
tiku_peer_mount_present(uint8_t index)
{
    tiku_peer_status_t st;

    return index < TIKU_PEER_MOUNT_PEERS && index < tiku_peer_count &&
           (mount_out & (uint8_t)(1u << index)) == 0u &&
           tiku_peer_service(index, "vfs") >= 0 &&
           tiku_peer_status(index, &st) == TIKU_PEER_OK &&
           st.state == TIKU_PEER_READY;
}

int
tiku_peer_mount_set(uint8_t index, uint8_t mounted)
{
    if (index >= TIKU_PEER_MOUNT_PEERS || index >= tiku_peer_count ||
        tiku_peer_service(index, "vfs") < 0) {
        return -1;
    }
    if (mounted != 0u) {
        mount_out &= (uint8_t)~(1u << index);
    } else {
        mount_out |= (uint8_t)(1u << index);
    }
    return 0;
}

/**
 * @brief Find the peer whose tree @p name is in: its first part is the name
 *        of a peer that is in /mnt.
 * @param rest  Receives the path under that peer; "" for its root
 * @return The peer's index, or -1
 */
static int
mount_route(const char *name, const char **rest)
{
    uint8_t i;

    for (i = 0; i < tiku_peer_count && i < TIKU_PEER_MOUNT_PEERS; i++) {
        const char *pn = tiku_peer_table[i]->name;
        size_t n = strlen(pn);

        if (strncmp(name, pn, n) == 0 && (name[n] == '\0' || name[n] == '/') &&
            tiku_peer_mount_present(i)) {
            *rest = (name[n] == '/') ? name + n + 1u : name + n;
            return (int)i;
        }
    }
    return -1;
}

/** @brief Read a file of a peer in /mnt. */
static int
mnt_read(const char *name, char *buf, size_t max)
{
    const char *rest;
    int i = mount_route(name, &rest);

    return (i < 0) ? TIKU_VFS_ENOENT : mount_read((uint8_t)i, rest, buf, max);
}

/** @brief Write a file of a peer in /mnt. */
static int
mnt_write(const char *name, const char *buf, size_t len)
{
    const char *rest;
    int i = mount_route(name, &rest);

    return (i < 0) ? TIKU_VFS_ENOENT : mount_write((uint8_t)i, rest, buf, len);
}

/** @brief Delete a file of a peer in /mnt. */
static int
mnt_unlink(const char *name)
{
    const char *rest;
    int i = mount_route(name, &rest);

    return (i < 0) ? TIKU_VFS_ENOENT : mount_unlink((uint8_t)i, rest);
}

/** @brief Read a file of a peer in /mnt from an offset. */
static int
mnt_read_at(const char *name, uint32_t offset, char *buf, size_t max,
            uint32_t *total)
{
    const char *rest;
    int i = mount_route(name, &rest);

    return (i < 0) ? TIKU_VFS_ENOENT
                   : mount_read_at((uint8_t)i, rest, offset, buf, max, total);
}

/** @brief List /mnt, the peers in it, or a directory of one of them. */
static int
mnt_list(const char *prefix, tiku_vfs_dyn_list_cb cb, void *ctx)
{
    const char *rest;
    int i;

    if (prefix[0] == '\0') {
        uint8_t p;

        for (p = 0; p < tiku_peer_count && p < TIKU_PEER_MOUNT_PEERS; p++) {
            if (tiku_peer_mount_present(p)) {
                char entry[TIKU_PEER_MOUNT_DATA + 1u];
                size_t n = strlen(tiku_peer_table[p]->name);

                if (n + 2u > sizeof(entry)) {
                    continue;
                }
                memcpy(entry, tiku_peer_table[p]->name, n);
                entry[n] = '/';
                entry[n + 1u] = '\0';
                cb(entry, ctx);
            }
        }
        return TIKU_VFS_OK;
    }
    i = mount_route(prefix, &rest);
    return (i < 0) ? TIKU_VFS_ENOENT : mount_list((uint8_t)i, rest, cb, ctx);
}

/** @brief 1 for a peer in /mnt or a directory of its tree, 0 for a file. */
static int
mnt_is_dir(const char *name)
{
    const char *rest;
    int i = mount_route(name, &rest);

    if (i < 0) {
        return TIKU_VFS_ENOENT;
    }
    return (rest[0] == '\0') ? 1 : mount_is_dir((uint8_t)i, rest);
}

static const tiku_vfs_dynops_t mnt_ops = {
    .read = mnt_read, .write = mnt_write, .unlink = mnt_unlink,
    .read_at = mnt_read_at, .list_checked = mnt_list, .is_dir = mnt_is_dir
};

static const tiku_vfs_node_t mount_root = {
    .name = "mnt", .type = TIKU_VFS_DIR, .dyn = &mnt_ops
};

const tiku_vfs_node_t *
tiku_peer_mount_get(void)
{
    return &mount_root;
}

#endif /* TIKU_PEER_ENABLE */
