/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_vfs.c - descriptors and directory streams over the kernel VFS,
 * read-only: tiku_vfs_read_at() for reads, tiku_vfs_list() for streams.
 * Each call's kernel work runs on the kernel thread (tiku_thread_call()).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "tiku_posix.h"
#include <kernel/threads/tiku_thread.h>
#include <kernel/vfs/tiku_vfs.h>

/* The tables, the read buffer and the authority are touched only on the
 * kernel thread, inside a call. */
typedef struct {
    uint8_t used;
    uint8_t dir;
    uint32_t offset;
    char path[TIKU_VFS_PATH_MAX];
} posix_fd_t;

typedef struct {
    uint8_t used;
    uint16_t position;
    char path[TIKU_VFS_PATH_MAX];
    struct dirent entry;
} posix_dir_t;

static posix_fd_t fds[TIKU_POSIX_FDS];
static posix_dir_t dirs[TIKU_POSIX_DIRS];
static char scratch[TIKU_POSIX_READ_MAX];
static tiku_vfs_cap_t authority = TIKU_VFS_CAP_NONE;

/* One call's arguments and results, on the caller's stack until it returns. */
struct posix_call;
typedef int (*posix_op_t)(struct posix_call *c);
struct posix_call {
    posix_op_t op;
    const char *path;
    int flags, fd, cmd, whence;
    void *buf;
    size_t n;
    off_t offset;
    struct stat *st;
    DIR *dir;
    struct dirent *entry;
    tiku_vfs_cap_t cap;
};

/** @brief The errno for a negative VFS status. */
static int vfs_errno(int status)
{
    switch (status) {
    case TIKU_VFS_ENOENT: return ENOENT;
    case TIKU_VFS_EACCES: return EACCES;
    case TIKU_VFS_EINVAL: return EINVAL;
    case TIKU_VFS_EPERM:  return EPERM;
    case TIKU_VFS_EBUSY:  return EBUSY;
    default:              return EIO;
    }
}

/** @brief 0 for an absolute path that fits the VFS, else a negative errno. */
static int path_ok(const char *path)
{
    size_t len = 0;

    if (path == NULL) return -EINVAL;
    if (path[0] != '/') return -ENOENT;
    while (path[len] != '\0') {
        if (++len >= TIKU_VFS_PATH_MAX) return -ENAMETOOLONG;
    }
    return 0;
}

/** @brief What @p path names: 1 a directory, 0 a readable file, or a negative
 *         errno.  A static node is not read, so a read with an effect is
 *         not made. */
static int node_kind(const char *path)
{
    const tiku_vfs_node_t *node;
    int r;

    if (tiku_vfs_is_dir(path)) return 1;
    node = tiku_vfs_resolve(path);
    if (node != NULL) {
        return (node->read != NULL ||
                (node->desc != NULL && node->desc->read_at != NULL)) ? 0 : -EACCES;
    }
    /* A dynamic directory's child: its first byte, from the store. */
    r = tiku_vfs_read_at(path, 0u, scratch, 1u, NULL);
    return r < 0 ? -vfs_errno(r) : 0;
}

static posix_fd_t *fd_slot(int fd)
{
    int i = fd - TIKU_POSIX_FD_BASE;

    return (i >= 0 && i < TIKU_POSIX_FDS && fds[i].used) ? &fds[i] : NULL;
}

static posix_dir_t *dir_slot(const DIR *dir)
{
    for (int i = 0; i < TIKU_POSIX_DIRS; i++) {
        if (dir == (const DIR *)&dirs[i]) return dirs[i].used ? &dirs[i] : NULL;
    }
    return NULL;
}

static uint32_t fnv(uint32_t h, const char *s, size_t n)
{
    while (n-- != 0u) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

/** @brief A node's number: FNV-1a over its path's components, each hashed
 *         as "/name", then @p child's when it is not NULL.  However the path
 *         is spelled, doubled or trailing slashes included, the same node
 *         gives the same number, never 0. */
static uint32_t node_id(const char *path, const char *child)
{
    uint32_t h = 2166136261u;
    const char *p = path, *start;
    int named = 0;

    while (*p != '\0') {
        while (*p == '/') p++;
        start = p;
        while (*p != '\0' && *p != '/') p++;
        if (p > start) {
            h = fnv(fnv(h, "/", 1u), start, (size_t)(p - start));
            named = 1;
        }
    }
    if (child != NULL) {
        h = fnv(fnv(h, "/", 1u), child, strlen(child));
        named = 1;
    }
    if (!named) h = fnv(h, "/", 1u);
    return h != 0u ? h : 1u;
}

static void fill_stat(struct stat *st, int dir, const char *path)
{
    memset(st, 0, sizeof *st);
    st->st_mode = dir ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    st->st_nlink = 1;
    st->st_ino = (ino_t)node_id(path, NULL);
}

static int op_open(struct posix_call *c)
{
    int kind = path_ok(c->path), write = (c->flags & O_ACCMODE) != O_RDONLY ||
               (c->flags & (O_CREAT | O_TRUNC | O_APPEND)) != 0;

    if (kind == 0) kind = node_kind(c->path);
    if (kind == -ENOENT && (c->flags & O_CREAT)) return -EROFS;
    if (kind < 0) return kind;
    if (kind == 1 && (c->flags & O_ACCMODE) != O_RDONLY) return -EISDIR;
    if (write) return -EROFS;
#ifdef O_DIRECTORY
    if ((c->flags & O_DIRECTORY) && kind != 1) return -ENOTDIR;
#endif
    for (int i = 0; i < TIKU_POSIX_FDS; i++) {
        if (!fds[i].used) {
            fds[i].used = 1u;
            fds[i].dir = (uint8_t)kind;
            fds[i].offset = 0u;
            strcpy(fds[i].path, c->path);
            return TIKU_POSIX_FD_BASE + i;
        }
    }
    return -EMFILE;
}

static int op_read(struct posix_call *c)
{
    posix_fd_t *f = fd_slot(c->fd);
    int r;
    size_t m;

    if (f == NULL) return -EBADF;
    if (f->dir) return -EISDIR;
    if (c->n == 0u) return 0;
    r = tiku_vfs_read_at(f->path, f->offset, scratch, sizeof scratch, NULL);
    if (r < 0) return -vfs_errno(r);
    m = ((size_t)r < c->n) ? (size_t)r : c->n;
    memcpy(c->buf, scratch, m);
    f->offset += (uint32_t)m;
    return (int)m;
}

static int op_lseek(struct posix_call *c)
{
    posix_fd_t *f = fd_slot(c->fd);
    long long to;

    if (f == NULL) return -EBADF;
    if (f->dir) return -EINVAL;
    if (c->whence == SEEK_SET) to = (long long)c->offset;
    else if (c->whence == SEEK_CUR) to = (long long)f->offset + (long long)c->offset;
    else return -EINVAL;
    if (to < 0 || to > INT_MAX) return -EINVAL;
    f->offset = (uint32_t)to;
    return (int)to;
}

static int op_close(struct posix_call *c)
{
    posix_fd_t *f = fd_slot(c->fd);

    if (f == NULL) return -EBADF;
    f->used = 0u;
    return 0;
}

static int op_fcntl(struct posix_call *c)
{
    if (fd_slot(c->fd) == NULL) return -EBADF;
    if (c->cmd == F_GETFL) return O_RDONLY;
    if (c->cmd == F_GETFD || c->cmd == F_SETFD) return 0;
    return -EINVAL;
}

static int op_fstat(struct posix_call *c)
{
    posix_fd_t *f = fd_slot(c->fd);

    if (f == NULL) return -EBADF;
    fill_stat(c->st, f->dir, f->path);
    return 0;
}

static int op_stat(struct posix_call *c)
{
    int kind = path_ok(c->path);

    if (kind == 0) kind = node_kind(c->path);
    if (kind < 0) return kind;
    fill_stat(c->st, kind, c->path);
    return 0;
}

static int op_is_open(struct posix_call *c)
{
    return fd_slot(c->fd) != NULL;
}

static int op_opendir(struct posix_call *c)
{
    int kind = path_ok(c->path);

    if (kind == 0) kind = node_kind(c->path);
    if (kind < 0) return kind;
    if (kind != 1) return -ENOTDIR;
    for (int i = 0; i < TIKU_POSIX_DIRS; i++) {
        if (!dirs[i].used) {
            dirs[i].used = 1u;
            dirs[i].position = 0u;
            strcpy(dirs[i].path, c->path);
            c->dir = (DIR *)&dirs[i];
            return 0;
        }
    }
    return -EMFILE;
}

/* tiku_vfs_list()'s walk for one entry: the child at position want. */
struct posix_pick {
    posix_dir_t *d;
    uint16_t want, seen;
    uint8_t found;
};

static void pick_child(const tiku_vfs_node_t *node, void *ctx)
{
    struct posix_pick *p = ctx;

    if (p->seen++ != p->want) return;
    strncpy(p->d->entry.d_name, node->name, sizeof p->d->entry.d_name - 1u);
    p->d->entry.d_name[sizeof p->d->entry.d_name - 1u] = '\0';
    p->d->entry.d_type = (node->type == TIKU_VFS_DIR) ? DT_DIR : DT_REG;
    p->d->entry.d_ino = (ino_t)node_id(p->d->path, p->d->entry.d_name);
    p->found = 1u;
}

static int op_readdir(struct posix_call *c)
{
    posix_dir_t *d = dir_slot(c->dir);
    struct posix_pick pick;
    int r;

    if (d == NULL) return -EBADF;
    pick.d = d;
    pick.want = d->position;
    pick.seen = 0u;
    pick.found = 0u;
    r = tiku_vfs_list(d->path, pick_child, &pick);
    if (r < 0 && !pick.found) return -vfs_errno(r);
    if (!pick.found) return 0;
    d->position++;
    c->entry = &d->entry;
    return 1;
}

static int op_rewinddir(struct posix_call *c)
{
    posix_dir_t *d = dir_slot(c->dir);

    if (d == NULL) return -EBADF;
    d->position = 0u;
    return 0;
}

static int op_closedir(struct posix_call *c)
{
    posix_dir_t *d = dir_slot(c->dir);

    if (d == NULL) return -EBADF;
    d->used = 0u;
    return 0;
}

static int op_is_dir_stream(struct posix_call *c)
{
    return dir_slot(c->dir) != NULL;
}

static int op_authority(struct posix_call *c)
{
    int previous = authority;

    authority = c->cap;
    return previous;
}

/* On the kernel thread: the operation under the library's authority, and
 * the caller's authority back on every return. */
static int run(void *arg)
{
    struct posix_call *c = arg;
    tiku_vfs_cap_t caller = tiku_vfs_caller_cap_set(authority);
    int r = c->op(c);

    (void)tiku_vfs_caller_cap_set(caller);
    return r;
}

/** @brief Run @p op on the kernel thread; a negative result sets errno on
 *         the caller's thread and returns -1. */
static int call(posix_op_t op, struct posix_call *c)
{
    int r;

    c->op = op;
    r = tiku_thread_call(run, c);
    if (r < 0) {
        errno = -r;
        return -1;
    }
    return r;
}

int tiku_posix_open(const char *path, int flags)
{
    struct posix_call c = { 0 };

    c.path = path;
    c.flags = flags;
    return call(op_open, &c);
}

ssize_t tiku_posix_read(int fd, void *buf, size_t n)
{
    struct posix_call c = { 0 };

    if (buf == NULL && n != 0u) {
        errno = EFAULT;
        return -1;
    }
    c.fd = fd;
    c.buf = buf;
    c.n = n;
    return call(op_read, &c);
}

off_t tiku_posix_lseek(int fd, off_t offset, int whence)
{
    struct posix_call c = { 0 };

    c.fd = fd;
    c.offset = offset;
    c.whence = whence;
    return call(op_lseek, &c);
}

int tiku_posix_close(int fd)
{
    struct posix_call c = { 0 };

    c.fd = fd;
    return call(op_close, &c);
}

int tiku_posix_fcntl(int fd, int cmd)
{
    struct posix_call c = { 0 };

    c.fd = fd;
    c.cmd = cmd;
    return call(op_fcntl, &c);
}

int tiku_posix_fstat(int fd, struct stat *st)
{
    struct posix_call c = { 0 };

    if (st == NULL) {
        errno = EFAULT;
        return -1;
    }
    c.fd = fd;
    c.st = st;
    return call(op_fstat, &c);
}

int tiku_posix_stat(const char *path, struct stat *st)
{
    struct posix_call c = { 0 };

    if (st == NULL) {
        errno = EFAULT;
        return -1;
    }
    c.path = path;
    c.st = st;
    return call(op_stat, &c);
}

int tiku_posix_is_open(int fd)
{
    struct posix_call c = { 0 };

    c.fd = fd;
    return call(op_is_open, &c) > 0;
}

DIR *tiku_posix_opendir(const char *path)
{
    struct posix_call c = { 0 };

    c.path = path;
    return call(op_opendir, &c) < 0 ? NULL : c.dir;
}

struct dirent *tiku_posix_readdir(DIR *dir)
{
    struct posix_call c = { 0 };

    c.dir = dir;
    return call(op_readdir, &c) > 0 ? c.entry : NULL;
}

void tiku_posix_rewinddir(DIR *dir)
{
    struct posix_call c = { 0 };

    c.dir = dir;
    (void)call(op_rewinddir, &c);
}

int tiku_posix_closedir(DIR *dir)
{
    struct posix_call c = { 0 };

    c.dir = dir;
    return call(op_closedir, &c);
}

int tiku_posix_is_dir_stream(const DIR *dir)
{
    struct posix_call c = { 0 };

    c.dir = (DIR *)(uintptr_t)dir;
    return call(op_is_dir_stream, &c) > 0;
}

uint8_t tiku_posix_authority_set(uint8_t cap)
{
    struct posix_call c = { 0 };

    c.cap = (tiku_vfs_cap_t)cap;
    return (uint8_t)call(op_authority, &c);
}
