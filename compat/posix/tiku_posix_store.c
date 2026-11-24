/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_store.c - compat/posix's store group: files of the kernel's file
 * store, written through private stages and published by the store's polled
 * writes while the calling worker waits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tiku_posix.h"
#include "tiku_posix_store.h"
#include <kernel/fs/tiku_tfs_async.h>
#include <kernel/threads/tiku_thread.h>
#include <kernel/vfs/tiku_vfs.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/vfs/tiku_vfs.h>
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>

/* A call's result while its change is still running: the worker yields and
 * calls again.  HELD: the store is held by a writer or a lease outside the
 * library, which a change waits for only TIKU_POSIX_STORE_WAIT_US. */
#define RUNNING INT_MAX
#define HELD    (INT_MAX - 2)

#define PREFIX_LEN (sizeof TIKU_POSIX_STORE_PREFIX - 1u)

typedef char store_stage_check[(TIKU_POSIX_STORE_FDS <= 32) ? 1 : -1];

/* A descriptor of the store.  The tables are touched only on the kernel
 * thread, inside a call. */
typedef struct {
    uint8_t used;
    uint8_t dir;
    uint8_t mode;         /* O_RDONLY, O_WRONLY or O_RDWR */
    uint8_t append;
    uint8_t dirty;        /* the stage holds what the store does not */
    uint8_t fresh;        /* created by this open, not yet in the store */
    uint8_t publishing;   /* the stage is a change's input */
    int8_t stage;         /* its stage, or -1 for a descriptor that reads */
    uint32_t offset;
    uint32_t length;
    char name[TIKU_TFS_NAME_MAX];
} store_fd_t;

/* The kinds of change a call waits for. */
enum { CHANGE_PUBLISH = 1, CHANGE_RENAME, CHANGE_DELETE };

struct store_call;
typedef int (*store_op_t)(struct store_call *c);
/* One call's arguments and results, on the caller's stack until it returns. */
struct store_call {
    store_op_t op;
    const char *path, *to;
    int flags, fd, whence, cmd, kind;
    void *buf;
    const void *data;
    size_t n;
    off_t offset;
    struct stat *st;
    uint64_t since;       /* when it first found the store held, or 0 */
    uint8_t worker;       /* made on a worker, which may wait */
    uint8_t begun;        /* its change is the one in flight */
    uint8_t close;        /* release the descriptor once published */
};

static store_fd_t sfds[TIKU_POSIX_STORE_FDS];
static uint8_t *stage_base;
static unsigned stage_count;
static uint32_t stage_busy;
static tiku_tfs_async_t change;
static const struct store_call *change_owner;
static const tiku_tfs_async_ops_t *change_ops;
static void *change_context;
static unsigned published, settled;

/** @brief The errno for a store error. */
static int tfs_errno(int rc)
{
    switch (rc) {
    case TFS_ERR_NOTFOUND: return ENOENT;
    case TFS_ERR_EXISTS:   return EEXIST;
    case TFS_ERR_NOSPACE:  return ENOSPC;
    case TFS_ERR_TOOBIG:   return EFBIG;
    case TFS_ERR_NAMELEN:  return ENAMETOOLONG;
    case TFS_ERR_BUSY:     return EBUSY;
    default:               return EIO;
    }
}

int tiku_posix_store_path(const char *path)
{
    return path != NULL && strncmp(path, TIKU_POSIX_STORE_PREFIX, PREFIX_LEN - 1u) == 0 &&
           (path[PREFIX_LEN - 1u] == '\0' || path[PREFIX_LEN - 1u] == '/');
}

int tiku_posix_store_fd(int fd)
{
    return fd >= TIKU_POSIX_STORE_FD_BASE &&
           fd < TIKU_POSIX_STORE_FD_BASE + TIKU_POSIX_STORE_FDS;
}

/**
 * @brief The store's name for @p path: "" for the store's directory.
 * @return 0, or -ENOENT for a path outside the store or a name that begins
 *         with '/', -ENAMETOOLONG for one the store cannot hold
 */
static int path_name(const char *path, const char **name)
{
    if (!tiku_posix_store_path(path)) return -ENOENT;
    *name = path[PREFIX_LEN - 1u] == '\0' ? "" : path + PREFIX_LEN;
    if ((*name)[0] == '/') return -ENOENT;
    return strlen(*name) < TIKU_TFS_NAME_MAX ? 0 : -ENAMETOOLONG;
}

/**
 * @brief The mounted store, remounted first when a failed change left it
 *        unmounted, so the remount settles that change.
 * @return the store, or NULL with *err -ENOENT without one, -EIO when it
 *         does not mount
 */
static tiku_tfs_t *store_get(int *err)
{
    tiku_tfs_t *fs = tiku_vfs_tree_data_store();

    if (fs == NULL) {
        *err = -ENOENT;
        return NULL;
    }
    if (!fs->mounted && !change.phase && tiku_tfs_mount(fs, fs->be) != TFS_OK) {
        *err = -EIO;
        return NULL;
    }
    return fs;
}

/** @brief Whether @p path is a static node of the VFS other than a dynamic
 *         directory, such as one the boot configuration adds under /data. */
static int foreign(const char *path)
{
    const tiku_vfs_node_t *node = tiku_vfs_resolve(path);

    return node != NULL && node->dyn == NULL;
}

/** @brief Whether a file name begins with the prefix @p ctx holds. */
typedef struct { const char *prefix; size_t n; int found; } prefix_t;
static void prefix_seen(const char *name, size_t len, void *ctx)
{
    prefix_t *p = ctx;

    (void)len;
    if (strncmp(name, p->prefix, p->n) == 0) p->found = 1;
}

/** @brief Whether @p name is a directory: the store's own, or a prefix of a
 *         file's name in the store or in a stage, followed by '/'. */
static int name_is_dir(tiku_tfs_t *fs, const char *name)
{
    char prefix[TIKU_TFS_NAME_MAX + 1];
    size_t n = strlen(name);
    prefix_t p;

    if (n == 0u) return 1;
    if (name[n - 1u] == '/') n--;
    memcpy(prefix, name, n);
    prefix[n] = '/';
    p.prefix = prefix;
    p.n = n + 1u;
    p.found = 0;
    (void)tiku_tfs_list(fs, prefix_seen, &p);
    for (int i = 0; i < TIKU_POSIX_STORE_FDS && !p.found; i++) {
        if (sfds[i].used && sfds[i].fresh) prefix_seen(sfds[i].name, 0u, &p);
    }
    return p.found;
}

static store_fd_t *fd_slot(int fd)
{
    int i = fd - TIKU_POSIX_STORE_FD_BASE;

    return (i >= 0 && i < TIKU_POSIX_STORE_FDS && sfds[i].used) ? &sfds[i] : NULL;
}

/** @brief The descriptor whose stage holds @p name, other than @p skip. */
static store_fd_t *writer_of(const char *name, const store_fd_t *skip)
{
    for (int i = 0; i < TIKU_POSIX_STORE_FDS; i++) {
        store_fd_t *f = &sfds[i];
        if (f != skip && f->used && f->stage >= 0 && strcmp(f->name, name) == 0) return f;
    }
    return NULL;
}

static uint8_t *stage_of(const store_fd_t *f)
{
    return stage_base + (size_t)f->stage * TIKU_POSIX_STAGE_MAX;
}

static void fd_release(store_fd_t *f)
{
    if (f->stage >= 0) stage_busy &= ~(1u << f->stage);
    memset(f, 0, sizeof *f);
}

/**
 * @brief The bytes @p name holds for the library: its writer's stage while
 *        one is open, else the store's content.
 * @return 0, or a negative errno
 */
static int content_of(tiku_tfs_t *fs, const char *name, const store_fd_t *self,
                      const uint8_t **p, uint32_t *len)
{
    const store_fd_t *w = (self != NULL && self->stage >= 0) ? self : writer_of(name, self);
    const void *mapped;
    size_t n;
    int rc;

    if (w != NULL) {
        *p = stage_of(w);
        *len = w->length;
        return 0;
    }
    rc = tiku_tfs_map(fs, name, &mapped, &n);
    if (rc != TFS_OK) return -tfs_errno(rc);
    *p = mapped;
    *len = (uint32_t)n;
    return 0;
}

static void fill_stat(struct stat *st, int dir, const char *name, uint32_t size)
{
    char path[sizeof TIKU_POSIX_STORE_PREFIX + TIKU_TFS_NAME_MAX];

    memcpy(path, TIKU_POSIX_STORE_PREFIX, PREFIX_LEN);
    strcpy(path + PREFIX_LEN, name);
    memset(st, 0, sizeof *st);
    st->st_mode = dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    st->st_nlink = 1;
    st->st_size = (off_t)size;
    st->st_ino = (ino_t)tiku_posix_node_number(path);
}

/*---------------------------------------------------------------------------*/
/* CALLS THAT DO NOT WAIT                                                    */
/*---------------------------------------------------------------------------*/

static int op_open(struct store_call *c)
{
    int acc = c->flags & O_ACCMODE, r = 0, i, exists, dir;
    int write = acc != O_RDONLY || (c->flags & (O_CREAT | O_TRUNC | O_APPEND)) != 0;
    const char *name;
    tiku_tfs_t *fs;
    store_fd_t *f, *other;
    size_t len = 0u;

    r = path_name(c->path, &name);
    if (r != 0) return r;
    if (foreign(c->path)) return TIKU_POSIX_STORE_FOREIGN;
    if (write && !(tiku_posix_authority() & TIKU_VFS_CAP_FS)) return -EPERM;
    fs = store_get(&r);
    if (fs == NULL) return r;
    dir = name_is_dir(fs, name);
    if (dir) {
        if (write) return -EISDIR;
    } else {
#ifdef O_DIRECTORY
        if (c->flags & O_DIRECTORY) return -ENOTDIR;
#endif
        if (name[strlen(name) - 1u] == '/') return -ENOTDIR;
    }
    for (i = 0; i < TIKU_POSIX_STORE_FDS && sfds[i].used; i++) {
    }
    if (i == TIKU_POSIX_STORE_FDS) return -EMFILE;
    f = &sfds[i];
    if (dir) {
        *f = (store_fd_t){ .used = 1u, .dir = 1u, .stage = -1 };
        strcpy(f->name, name);
        return TIKU_POSIX_STORE_FD_BASE + i;
    }
    other = writer_of(name, NULL);
    exists = (other != NULL && other->fresh) || tiku_tfs_stat(fs, name, &len) == TFS_OK;
    if (other != NULL) len = other->length;
    if (!exists && !(c->flags & O_CREAT)) return -ENOENT;
    if (exists && (c->flags & O_CREAT) && (c->flags & O_EXCL)) return -EEXIST;
    if (write && other != NULL) return -EBUSY;
    *f = (store_fd_t){ .used = 1u, .mode = (uint8_t)acc, .stage = -1,
                       .append = (uint8_t)((c->flags & O_APPEND) != 0) };
    strcpy(f->name, name);
    if (write) {
        unsigned s;
        for (s = 0u; s < stage_count && (stage_busy & (1u << s)); s++) {
        }
        if (s == stage_count) {
            memset(f, 0, sizeof *f);
            return -ENFILE;
        }
        if (exists && !(c->flags & O_TRUNC)) {
            if (len > TIKU_POSIX_STAGE_MAX) {
                memset(f, 0, sizeof *f);
                return -EFBIG;
            }
            if (tiku_tfs_read(fs, name, stage_base + (size_t)s * TIKU_POSIX_STAGE_MAX,
                              TIKU_POSIX_STAGE_MAX, &len) != TFS_OK) {
                memset(f, 0, sizeof *f);
                return -EIO;
            }
        } else {
            len = 0u;
        }
        stage_busy |= 1u << s;
        f->stage = (int8_t)s;
        f->fresh = (uint8_t)!exists;
        f->dirty = (uint8_t)(!exists || (c->flags & O_TRUNC) != 0);
    }
    f->length = (uint32_t)len;
    return TIKU_POSIX_STORE_FD_BASE + i;
}

static int op_write(struct store_call *c)
{
    store_fd_t *f = fd_slot(c->fd);
    uint32_t at;
    size_t m;

    if (f == NULL || f->dir || f->mode == O_RDONLY || f->stage < 0) return -EBADF;
    if (f->publishing) return -EBUSY;
    at = f->append ? f->length : f->offset;
    if (c->n == 0u) return 0;
    if (at >= TIKU_POSIX_STAGE_MAX) return -EFBIG;
    m = TIKU_POSIX_STAGE_MAX - at;
    if (m > c->n) m = c->n;
    if (at > f->length) memset(stage_of(f) + f->length, 0, at - f->length);
    memcpy(stage_of(f) + at, c->data, m);
    if (at + m > f->length) f->length = at + (uint32_t)m;
    f->offset = at + (uint32_t)m;
    f->dirty = 1u;
    return (int)m;
}

static int op_read(struct store_call *c)
{
    store_fd_t *f = fd_slot(c->fd);
    const uint8_t *p;
    uint32_t len;
    tiku_tfs_t *fs;
    int r = 0;
    size_t m;

    if (f == NULL || f->mode == O_WRONLY) return -EBADF;
    if (f->dir) return -EISDIR;
    fs = store_get(&r);
    if (fs == NULL) return r;
    r = content_of(fs, f->name, f, &p, &len);
    if (r != 0) return r;
    if (f->offset >= len || c->n == 0u) return 0;
    m = len - f->offset;
    if (m > c->n) m = c->n;
    if (m > INT_MAX) m = INT_MAX;
    memcpy(c->buf, p + f->offset, m);
    f->offset += (uint32_t)m;
    return (int)m;
}

static int op_lseek(struct store_call *c)
{
    store_fd_t *f = fd_slot(c->fd);
    const uint8_t *p;
    uint32_t len = 0u;
    long long to;
    tiku_tfs_t *fs;
    int r = 0;

    if (f == NULL) return -EBADF;
    if (f->dir) return -EINVAL;
    if (c->whence == SEEK_END) {
        fs = store_get(&r);
        if (fs == NULL) return r;
        r = content_of(fs, f->name, f, &p, &len);
        if (r != 0) return r;
    }
    if (c->whence == SEEK_SET) to = (long long)c->offset;
    else if (c->whence == SEEK_CUR) to = (long long)f->offset + (long long)c->offset;
    else if (c->whence == SEEK_END) to = (long long)len + (long long)c->offset;
    else return -EINVAL;
    if (to < 0 || to > INT_MAX) return -EINVAL;
    f->offset = (uint32_t)to;
    return (int)to;
}

static int op_fcntl(struct store_call *c)
{
    store_fd_t *f = fd_slot(c->fd);

    if (f == NULL) return -EBADF;
    if (c->cmd == F_GETFL) return f->mode | (f->append ? O_APPEND : 0);
    if (c->cmd == F_GETFD || c->cmd == F_SETFD) return 0;
    return -EINVAL;
}

static int op_fstat(struct store_call *c)
{
    store_fd_t *f = fd_slot(c->fd);
    const uint8_t *p;
    uint32_t len = 0u;
    tiku_tfs_t *fs;
    int r = 0;

    if (f == NULL) return -EBADF;
    if (!f->dir) {
        fs = store_get(&r);
        if (fs == NULL) return r;
        r = content_of(fs, f->name, f, &p, &len);
        if (r != 0) return r;
    }
    fill_stat(c->st, f->dir, f->name, len);
    return 0;
}

static int op_stat(struct store_call *c)
{
    const char *name;
    const uint8_t *p;
    uint32_t len = 0u;
    tiku_tfs_t *fs;
    int r = path_name(c->path, &name), dir;

    if (r != 0) return r;
    if (foreign(c->path)) return TIKU_POSIX_STORE_FOREIGN;
    fs = store_get(&r);
    if (fs == NULL) return r;
    dir = name_is_dir(fs, name);
    if (!dir) {
        r = content_of(fs, name, NULL, &p, &len);
        if (r != 0) return r;
    }
    fill_stat(c->st, dir, name, len);
    return 0;
}

static int op_is_open(struct store_call *c)
{
    return fd_slot(c->fd) != NULL;
}

/*---------------------------------------------------------------------------*/
/* CHANGES, WHICH WAIT                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Begin @p c's change as the one in flight.
 * @return 1 begun; 0 when there is nothing to change; RUNNING while the
 *         library's own change runs; HELD while a writer or a lease outside
 *         the library holds the store; or a negative errno
 */
static int change_begin(struct store_call *c, tiku_tfs_t *fs)
{
    const char *from = NULL, *to = NULL;
    store_fd_t *f = NULL;
    int rc, r;

    if (c->kind == CHANGE_PUBLISH) {
        f = fd_slot(c->fd);
        if (f == NULL) return -EBADF;
        if (f->dir || f->stage < 0 || !f->dirty) return 0;
    } else {
        r = path_name(c->path, &from);
        if (r != 0) return r;
        if (name_is_dir(fs, from)) return -EISDIR;
        if (writer_of(from, NULL) != NULL) return -EBUSY;
        if (c->kind == CHANGE_RENAME) {
            r = path_name(c->to, &to);
            if (r != 0) return r;
            if (name_is_dir(fs, to)) return -EISDIR;
            if (writer_of(to, NULL) != NULL) return -EBUSY;
        }
        if (tiku_tfs_stat(fs, from, &(size_t){ 0u }) != TFS_OK) return -ENOENT;
        if (to != NULL && strcmp(from, to) == 0) return 0;
    }
    if (!c->worker) return -EAGAIN;
    if (change.phase || change_owner != NULL) return RUNNING;
    change_ops = tiku_tfs_async_port(&change_context);
    memset(&change, 0, sizeof change);
    if (c->kind == CHANGE_PUBLISH) {
        rc = tiku_tfs_async_begin(fs, &change, f->name, stage_of(f), f->length,
                                  change_ops, change_context);
    } else if (c->kind == CHANGE_RENAME) {
        rc = tiku_tfs_async_rename(fs, &change, from, to, change_ops, change_context);
    } else {
        rc = tiku_tfs_async_delete(fs, &change, from, change_ops, change_context);
    }
    if (rc == TFS_ERR_BUSY) return HELD;
    if (rc != TFS_OK) return -tfs_errno(rc);
    if (f != NULL) f->publishing = 1u;
    change_owner = c;
    c->begun = 1u;
    return 1;
}

/**
 * @brief Decide a change whose request failed, from the store as a remount
 *        finds it: a published stage matches the store's content, a rename
 *        or a delete left no source.
 * @return 0 when the change is in the store, else -EIO
 */
static int change_settle(struct store_call *c, tiku_tfs_t *fs)
{
    const char *from;
    store_fd_t *f;
    const void *p;
    size_t n;

    if (tiku_tfs_mount(fs, fs->be) != TFS_OK) return -EIO;
    if (c->kind == CHANGE_PUBLISH) {
        f = fd_slot(c->fd);
        if (f == NULL || tiku_tfs_map(fs, f->name, &p, &n) != TFS_OK ||
            n != f->length || memcmp(p, stage_of(f), n) != 0) {
            return -EIO;
        }
    } else if (path_name(c->path, &from) != 0 ||
               tiku_tfs_stat(fs, from, &n) != TFS_ERR_NOTFOUND) {
        return -EIO;
    }
    settled++;
    return 0;
}

/** @brief Step the change in flight for up to TIKU_POSIX_STORE_BUDGET_US,
 *         then report it: RUNNING, or its result. */
static int change_step(struct store_call *c, tiku_tfs_t *fs)
{
    uint64_t start = tiku_clock_usec();
    store_fd_t *f = c->kind == CHANGE_PUBLISH ? fd_slot(c->fd) : NULL;
    int rc, r;

    do {
        rc = tiku_tfs_async_step(&change);
    } while (rc == 1 && tiku_clock_usec() - start < TIKU_POSIX_STORE_BUDGET_US);
    if (rc == 1) return RUNNING;
    change_owner = NULL;
    if (f != NULL) f->publishing = 0u;
    if (rc < 0) return -EIO;
    r = change.result == TFS_OK ? 0 : change_settle(c, fs);
    if (r == 0) {
        /* A record of /data, as the VFS makes for a write of one of its
         * files, so the change ring's readers see the library's changes. */
        char root[PREFIX_LEN];

        memcpy(root, TIKU_POSIX_STORE_PREFIX, PREFIX_LEN - 1u);
        root[PREFIX_LEN - 1u] = '\0';
        tiku_vfs_notify(tiku_vfs_resolve(root));
        published++;
        if (f != NULL) {
            f->dirty = 0u;
            f->fresh = 0u;
        }
    }
    return r;
}

/* A change: begun when the store is free, then stepped a budget a call. */
static int op_change(struct store_call *c)
{
    tiku_tfs_t *fs;
    store_fd_t *f;
    int r = 0;

    if (!c->begun) {
        if (c->kind != CHANGE_PUBLISH && !(tiku_posix_authority() & TIKU_VFS_CAP_FS)) {
            return -EPERM;
        }
        fs = store_get(&r);
        if (fs == NULL) return r;
        r = change_begin(c, fs);
        if (r == HELD) {
            uint64_t now = tiku_clock_usec();
            if (c->since == 0u) c->since = now;
            r = (now - c->since >= TIKU_POSIX_STORE_WAIT_US) ? -EBUSY : RUNNING;
        } else if (r == RUNNING) {
            c->since = 0u;
        }
        if (r <= 0 || r == RUNNING) {
            if (r == 0 && c->close && (f = fd_slot(c->fd)) != NULL) fd_release(f);
            return r;
        }
    }
    fs = tiku_vfs_tree_data_store();
    r = change_step(c, fs);
    if (r != RUNNING && c->close && (f = fd_slot(c->fd)) != NULL) fd_release(f);
    return r;
}

/* On the kernel thread: the call's operation. */
static int run(void *arg)
{
    struct store_call *c = arg;

    return c->op(c);
}

/**
 * @brief Run @p op on the kernel thread, again after a yield while its
 *        change runs; a negative result sets errno and returns -1.
 */
static int call(store_op_t op, struct store_call *c)
{
    int r;

    c->op = op;
    c->worker = (uint8_t)!tiku_thread_in_kernel();
    for (;;) {
        r = tiku_thread_call(run, c);
        if (r != RUNNING) break;
        tiku_thread_yield();
    }
    if (r < 0) {
        errno = -r;
        return -1;
    }
    return r;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC CALLS                                                              */
/*---------------------------------------------------------------------------*/

int tiku_posix_store_memory(void *base, size_t bytes)
{
    if (base == NULL || bytes < TIKU_POSIX_STAGE_MAX) return EINVAL;
    if (stage_base != NULL) return EBUSY;
    stage_base = base;
    stage_count = (unsigned)(bytes / TIKU_POSIX_STAGE_MAX);
    if (stage_count > TIKU_POSIX_STORE_FDS) stage_count = TIKU_POSIX_STORE_FDS;
    return 0;
}

int tiku_posix_store_open(const char *path, int flags)
{
    struct store_call c = { 0 };

    c.path = path;
    c.flags = flags;
    return call(op_open, &c);
}

ssize_t tiku_posix_store_read(int fd, void *buf, size_t n)
{
    struct store_call c = { 0 };

    if (buf == NULL && n != 0u) {
        errno = EFAULT;
        return -1;
    }
    c.fd = fd;
    c.buf = buf;
    c.n = n;
    return call(op_read, &c);
}

off_t tiku_posix_store_lseek(int fd, off_t offset, int whence)
{
    struct store_call c = { 0 };

    c.fd = fd;
    c.offset = offset;
    c.whence = whence;
    return call(op_lseek, &c);
}

int tiku_posix_store_close(int fd)
{
    struct store_call c = { 0 };

    c.fd = fd;
    c.kind = CHANGE_PUBLISH;
    c.close = 1u;
    return call(op_change, &c);
}

int tiku_posix_store_fcntl(int fd, int cmd)
{
    struct store_call c = { 0 };

    c.fd = fd;
    c.cmd = cmd;
    return call(op_fcntl, &c);
}

int tiku_posix_store_fstat(int fd, struct stat *st)
{
    struct store_call c = { 0 };

    if (st == NULL) {
        errno = EFAULT;
        return -1;
    }
    c.fd = fd;
    c.st = st;
    return call(op_fstat, &c);
}

int tiku_posix_store_stat(const char *path, struct stat *st)
{
    struct store_call c = { 0 };

    if (st == NULL) {
        errno = EFAULT;
        return -1;
    }
    c.path = path;
    c.st = st;
    return call(op_stat, &c);
}

int tiku_posix_store_is_open(int fd)
{
    struct store_call c = { 0 };

    c.fd = fd;
    return call(op_is_open, &c) > 0;
}

ssize_t tiku_posix_store_write(int fd, const void *buf, size_t n)
{
    struct store_call c = { 0 };

    if (buf == NULL && n != 0u) {
        errno = EFAULT;
        return -1;
    }
    c.fd = fd;
    c.data = buf;
    c.n = n;
    return call(op_write, &c);
}

int tiku_posix_fsync(int fd)
{
    struct store_call c = { 0 };

    if (!tiku_posix_store_fd(fd)) {
        errno = tiku_posix_is_open(fd) ? EINVAL : EBADF;
        return -1;
    }
    c.fd = fd;
    c.kind = CHANGE_PUBLISH;
    return call(op_change, &c);
}

int tiku_posix_rename(const char *from, const char *to)
{
    struct store_call c = { 0 };
    int a = tiku_posix_store_path(from), b = tiku_posix_store_path(to);

    if (from == NULL || to == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (!a || !b) {
        errno = (a || b) ? EXDEV : EROFS;
        return -1;
    }
    c.path = from;
    c.to = to;
    c.kind = CHANGE_RENAME;
    return call(op_change, &c);
}

int tiku_posix_unlink(const char *path)
{
    struct store_call c = { 0 };

    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (!tiku_posix_store_path(path)) {
        errno = EROFS;
        return -1;
    }
    c.path = path;
    c.kind = CHANGE_DELETE;
    return call(op_change, &c);
}

void tiku_posix_store_use(unsigned *made, unsigned *decided)
{
    if (made != NULL) *made = published;
    if (decided != NULL) *decided = settled;
}
