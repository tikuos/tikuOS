/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_mount_serve.c - this side's file tree, served to a peer.
 *
 * One request in, one answer out, through the VFS calls a local reader
 * uses.  /mnt is left out, so two sides that mount each other form no
 * loop, and a secret node is not read.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include "tiku_peer_mount.h"
#include <string.h>

#if (TIKU_PEER_ENABLE + 0)

/** @brief Ticks a held file part stays usable after its last use. */
#define HOLD_TICKS  ((tiku_clock_time_t)(2u * TIKU_CLOCK_SECOND))

/** @brief Read a 32-bit little-endian value. */
static uint32_t
get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/** @brief Store a 32-bit little-endian value. */
static void
put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/** @brief The path past its leading slashes. */
static const char *
past_slashes(const char *path)
{
    while (*path == '/') {
        path++;
    }
    return path;
}

/** @brief 1 for /mnt and what is under it: this side's own mounts. */
static uint8_t
hidden(const char *path)
{
    path = past_slashes(path);
    return (uint8_t)(strncmp(path, "mnt", 3) == 0 &&
                     (path[3] == '\0' || path[3] == '/'));
}

/*---------------------------------------------------------------------------*/
/* LIST                                                                      */
/*---------------------------------------------------------------------------*/

/** @brief One LIST answer being filled. */
typedef struct {
    uint8_t *out;      /* the answer's data */
    uint8_t  used;
    uint8_t  full;     /* an entry did not fit: it and the rest wait */
    uint8_t  root;     /* listing "/": the entry "mnt" is left out */
    uint32_t skip;     /* entries still to pass over */
    uint32_t passed;   /* entries this answer accounts for */
} list_t;

/**
 * @brief Add one child's name and its NUL; a directory's name ends in '/'.
 * @note A name too long for an empty answer is counted and left out.
 */
static void
list_one(const tiku_vfs_node_t *node, void *ctx)
{
    list_t *l = (list_t *)ctx;
    size_t n, need;

    if (node == (const tiku_vfs_node_t *)0 || node->name == (const char *)0 ||
        l->full != 0u) {
        return;
    }
    if (l->root != 0u && strcmp(node->name, "mnt") == 0) {
        return;
    }
    if (l->skip != 0u) {
        l->skip--;
        return;
    }
    n = strlen(node->name);
    need = n + ((node->type == TIKU_VFS_DIR) ? 2u : 1u);
    if (need > TIKU_PEER_MOUNT_DATA) {
        l->passed++;
        return;
    }
    if ((size_t)l->used + need > TIKU_PEER_MOUNT_DATA) {
        l->full = 1u;
        return;
    }
    memcpy(l->out + l->used, node->name, n);
    l->used = (uint8_t)(l->used + n);
    if (node->type == TIKU_VFS_DIR) {
        l->out[l->used++] = (uint8_t)'/';
    }
    l->out[l->used++] = 0u;
    l->passed++;
}

/*---------------------------------------------------------------------------*/
/* STAT AND READ                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Whether @p path is a directory, a file or neither. */
static int
stat_path(const char *path, uint8_t *flags)
{
    const tiku_vfs_node_t *node = tiku_vfs_resolve(path);
    char probe[1];

    if (node != (const tiku_vfs_node_t *)0) {
        if (node->type == TIKU_VFS_DIR) {
            *flags |= TIKU_PEER_MOUNT_F_DIR;
        }
        return TIKU_VFS_OK;
    }
    if (tiku_vfs_is_dir(path) == 1) {
        *flags |= TIKU_PEER_MOUNT_F_DIR;
        return TIKU_VFS_OK;
    }
    return (tiku_vfs_read(path, probe, sizeof(probe)) >= 0) ? TIKU_VFS_OK
                                                            : TIKU_VFS_ENOENT;
}

/**
 * @brief Copy up to TIKU_PEER_MOUNT_DATA bytes of @p path from @p offset.
 *
 * A read from offset 0 renders the file into hold[]; the parts after it
 * come from hold[] while it is fresh, so one read sees one rendering.
 *
 * @return Bytes copied, 0 at the end, or a negative TIKU_VFS_* status
 */
static int
read_part(tiku_peer_mount_server_t *s, const char *path, uint32_t offset,
          uint8_t *out, uint32_t *total)
{
    const tiku_vfs_node_t *node = tiku_vfs_resolve(path);
    tiku_clock_time_t now = tiku_clock_time();
    uint32_t end = s->held_base + s->held;
    uint16_t give;

    if (node != (const tiku_vfs_node_t *)0 &&
        node->desc != (const tiku_vfs_desc_t *)0 &&
        (node->desc->flags & TIKU_VFS_DF_SECRET) != 0u) {
        return TIKU_VFS_EACCES;
    }
    if (!(offset != 0u && s->held_valid != 0u &&
          strcmp(s->held_path, path) == 0 &&
          (tiku_clock_time_t)(now - s->held_at) < HOLD_TICKS &&
          offset >= s->held_base &&
          (offset < end || (offset == end && offset >= s->held_total)))) {
        uint32_t t = 0u;
        int n;

        s->held_valid = 0u;
        n = tiku_vfs_read_at(path, offset, s->hold, sizeof(s->hold), &t);
        if (n < 0) {
            return n;
        }
        strcpy(s->held_path, path);
        s->held_base = offset;
        s->held = (uint16_t)n;
        s->held_total = t;
        s->held_valid = 1u;
    }
    s->held_at = now;
    give = (uint16_t)(s->held_base + s->held - offset);
    if (give > TIKU_PEER_MOUNT_DATA) {
        give = TIKU_PEER_MOUNT_DATA;
    }
    memcpy(out, s->hold + (offset - s->held_base), give);
    *total = s->held_total;
    return (int)give;
}

/*---------------------------------------------------------------------------*/
/* WRITE                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Write @p len bytes to @p path under the peer's capabilities. */
static int
write_whole(tiku_peer_mount_server_t *s, const char *path, const char *data,
            uint16_t len)
{
    tiku_vfs_cap_t before = tiku_vfs_caller_cap_set(s->cap);
    int rc = tiku_vfs_write(path, data, len);

    (void)tiku_vfs_caller_cap_set(before);
    return rc;
}

/**
 * @brief Take a WRITE: do it when the request holds all of it, else keep
 *        its start for the MORE requests that follow.
 * @return A TIKU_VFS_* status; *more set when parts are still to come
 */
static int
write_first(tiku_peer_mount_server_t *s, const char *path, uint32_t total,
            const uint8_t *data, uint8_t have, uint8_t *more)
{
    s->write_open = 0u;
    if (total > TIKU_PEER_MOUNT_WRITE_MAX) {
        return TIKU_VFS_E2BIG;
    }
    if (have > total) {
        return TIKU_VFS_EINVAL;
    }
    if (have == total) {
        return write_whole(s, path, (const char *)data, (uint16_t)total);
    }
    strcpy(s->write_path, path);
    memcpy(s->write, data, have);
    s->write_total = (uint16_t)total;
    s->write_have = have;
    s->write_open = 1u;
    *more = 1u;
    return TIKU_VFS_OK;
}

/** @brief Take a MORE: the next part, at the offset the parts so far end. */
static int
write_next(tiku_peer_mount_server_t *s, uint32_t offset, const uint8_t *data,
           uint8_t have, uint8_t *more)
{
    if (s->write_open == 0u || offset != s->write_have ||
        have == 0u || have > (uint16_t)(s->write_total - s->write_have)) {
        s->write_open = 0u;
        return TIKU_VFS_EINVAL;
    }
    memcpy(s->write + s->write_have, data, have);
    s->write_have = (uint16_t)(s->write_have + have);
    if (s->write_have < s->write_total) {
        *more = 1u;
        return TIKU_VFS_OK;
    }
    s->write_open = 0u;
    return write_whole(s, s->write_path, s->write, s->write_total);
}

/*---------------------------------------------------------------------------*/
/* ONE REQUEST                                                               */
/*---------------------------------------------------------------------------*/

uint8_t
tiku_peer_mount_serve(tiku_peer_mount_server_t *s, const uint8_t *request,
                      uint8_t length, uint8_t *answer)
{
    char path[TIKU_PEER_MOUNT_DATA + 1u];
    const uint8_t *data;
    uint32_t arg, result = 0u;
    uint8_t op, plen, have, flags = 0u, used = 0u, more = 0u;
    int rc;

    if (s == (tiku_peer_mount_server_t *)0 || request == (const uint8_t *)0 ||
        answer == (uint8_t *)0 || length < TIKU_PEER_MOUNT_HEAD ||
        length > TIKU_PEER_MOUNT_MSG || request[0] != TIKU_PEER_MOUNT_REQUEST) {
        return 0u;
    }
    op = request[2];
    plen = request[3];
    arg = get32(request + 4);
    if (plen > (uint8_t)(length - TIKU_PEER_MOUNT_HEAD) ||
        memchr(request + TIKU_PEER_MOUNT_HEAD, 0, plen) != (void *)0) {
        rc = TIKU_VFS_EINVAL;
    } else {
        memcpy(path, request + TIKU_PEER_MOUNT_HEAD, plen);
        path[plen] = '\0';
        data = request + TIKU_PEER_MOUNT_HEAD + plen;
        have = (uint8_t)(length - TIKU_PEER_MOUNT_HEAD - plen);
        if (op == TIKU_PEER_MOUNT_MORE) {
            s->held_valid = 0u;
            rc = (plen == 0u) ? write_next(s, arg, data, have, &more)
                              : TIKU_VFS_EINVAL;
        } else if (path[0] != '/') {
            rc = TIKU_VFS_EINVAL;
        } else if (hidden(path)) {
            rc = TIKU_VFS_ENOENT;
        } else if (op == TIKU_PEER_MOUNT_LIST) {
            list_t l;

            memset(&l, 0, sizeof(l));
            l.out = answer + TIKU_PEER_MOUNT_HEAD;
            l.root = (uint8_t)(*past_slashes(path) == '\0');
            l.skip = arg;
            rc = tiku_vfs_list(path, list_one, &l);
            if (rc > 0) {
                rc = TIKU_VFS_OK;
            }
            used = l.used;
            result = l.passed;
            more = l.full;
        } else if (op == TIKU_PEER_MOUNT_STAT) {
            rc = stat_path(path, &flags);
        } else if (op == TIKU_PEER_MOUNT_READ) {
            rc = read_part(s, path, arg, answer + TIKU_PEER_MOUNT_HEAD,
                           &result);
            if (rc >= 0) {
                used = (uint8_t)rc;
                rc = TIKU_VFS_OK;
            }
        } else if (op == TIKU_PEER_MOUNT_WRITE) {
            s->held_valid = 0u;
            rc = write_first(s, path, arg, data, have, &more);
        } else if (op == TIKU_PEER_MOUNT_UNLINK) {
            tiku_vfs_cap_t before = tiku_vfs_caller_cap_set(s->cap);

            s->held_valid = 0u;
            rc = tiku_vfs_unlink(path);
            (void)tiku_vfs_caller_cap_set(before);
        } else {
            rc = TIKU_VFS_ENOTSUP;
        }
    }
    if (rc < -128) {
        rc = TIKU_VFS_ERR;
    }
    if (more != 0u) {
        flags |= TIKU_PEER_MOUNT_F_MORE;
    }
    answer[0] = TIKU_PEER_MOUNT_ANSWER;
    answer[1] = request[1];
    answer[2] = (uint8_t)(int8_t)rc;
    answer[3] = flags;
    put32(answer + 4, result);
    return (uint8_t)(TIKU_PEER_MOUNT_HEAD + used);
}

#endif /* TIKU_PEER_ENABLE */
