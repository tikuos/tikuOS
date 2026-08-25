/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs.c - file operations, stepped rather than blocking.
 *
 * An operation is a queue plus a cursor: tiku_fs_step() does a little and
 * returns, so the window keeps drawing and a collision can stop the queue and
 * wait for an answer instead of guessing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs.h"
#include "tiku_mount.h"
#include "tiku_state.h"
#include "tiku_volume.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define FS_QUEUE_MAX 512

/** @brief One reversal record: how to put a single item back. */
typedef struct {
    char from[TIKU_PATH_MAX];   /* where it ended up */
    char to[TIKU_PATH_MAX];     /* where it came from, "" if created */
    int  created;                   /* undo removes rather than moves    */
} undo_rec_t;

/**
 * @brief One reversible operation: its kind and the records it produced.
 *
 * A whole operation is the unit, not a record: undoing half a move of nine
 * items is not something the user ever asked for.
 */
typedef struct {
    tiku_op_t op;
    int           n;
    undo_rec_t   *rec;              /* n entries, grown as they arrive   */
} undo_op_t;

/**
 * @brief How deep the two stacks go.
 *
 * Tracker's kUndoRedoListMaxCount.  A bound rather than a growing list
 * because the records hold paths, and an unbounded history of them is a
 * leak that only shows up on a long-lived session.
 */
#define FS_UNDO_MAX 20


static void undo_stack_clear(undo_op_t *stack, int *count);

struct tiku_fs {
    tiku_backend_t *backend;
    char                queue[FS_QUEUE_MAX][TIKU_PATH_MAX];
    /* Set once a folder's children have been queued ahead of its second
     * visit, so a folder whose children WILL NOT go is not expanded again
     * forever (FS-049). */
    unsigned char       qexpanded[FS_QUEUE_MAX];
    int                 qcount;
    int                 qcursor;
    char                dst[TIKU_PATH_MAX];
    tiku_conflict_t policy;
    tiku_progress_t prog;

    int                 pending_ask;  /* a one-shot answer is in force  */
    undo_op_t           ustack[FS_UNDO_MAX];
    int                 ucount;
    undo_op_t           rstack[FS_UNDO_MAX];
    int                 rcount;

    tiku_cancel_fn  cancel_fn;
    void               *cancel_ctx;
    tiku_fs_moved_fn moved_fn;
    void               *moved_ctx;
    int                 cancelling;   /* the flag cancel() itself sets  */
    int                 link_relative;
};

/** @brief An item is no longer at @p from; it is at @p to. */
static void
landed(tiku_fs_t *fs, const char *from, const char *to)
{
    if (fs != NULL && fs->moved_fn != NULL && from != NULL && to != NULL &&
        from[0] != '\0' && to[0] != '\0') {
        fs->moved_fn(fs->moved_ctx, from, to);
    }
}

void
tiku_fs_set_moved(tiku_fs_t *fs, tiku_fs_moved_fn fn, void *ctx)
{
    if (fs != NULL) {
        fs->moved_fn = fn;
        fs->moved_ctx = ctx;
    }
}

/** @brief Whether the job should stop where it stands. */
static int
cancelled(tiku_fs_t *fs)
{
    if (fs->cancelling) {
        return 1;
    }
    if (fs->cancel_fn != NULL && fs->cancel_fn(fs->cancel_ctx)) {
        fs->cancelling = 1;
    }
    return fs->cancelling;
}

tiku_fs_t *
tiku_fs_new(tiku_backend_t *backend)
{
    tiku_fs_t *fs = calloc(1, sizeof *fs);

    if (fs != NULL) {
        fs->backend = backend;
    }
    return fs;
}

void
tiku_fs_free(tiku_fs_t *fs)
{
    if (fs != NULL) {
        undo_stack_clear(fs->ustack, &fs->ucount);
        undo_stack_clear(fs->rstack, &fs->rcount);
    }
    if (fs != NULL) {
        free(fs);
    }
}

int
tiku_fs_busy(const tiku_fs_t *fs)
{
    return (fs != NULL && fs->prog.op != TIKU_OP_IDLE &&
            fs->qcursor < fs->qcount);
}

const tiku_progress_t *
tiku_fs_progress(const tiku_fs_t *fs)
{
    return (fs != NULL) ? &fs->prog : NULL;
}

/*---------------------------------------------------------------------------*/
/* names                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief The one place the illegal set is written down. */
#define NAME_DENY "/"

const char *
tiku_fs_name_deny(void)
{
    return NAME_DENY;
}

int
tiku_fs_name_ok(const char *name, char *err, size_t errmax)
{
    if (name == NULL || name[0] == '\0') {
        if (err != NULL) { snprintf(err, errmax, "a name cannot be empty"); }
        return 0;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        if (err != NULL) { snprintf(err, errmax, "'%s' is not a usable name",
                                    name); }
        return 0;
    }
    if (strpbrk(name, NAME_DENY) != NULL) {
        if (err != NULL) {
            snprintf(err, errmax, "a name cannot contain '/'");
        }
        return 0;
    }
    return 1;
}

/** @brief Join a directory and a leaf name; -1 rather than truncate. */
static int
join(char *out, size_t max, const char *dir, const char *name)
{
    size_t dl = strlen(dir), nl = strlen(name);
    int root = (dir[0] == '/' && dir[1] == '\0');

    if (dl + nl + 2u > max) {
        return -1;
    }
    if (root) {
        out[0] = '/';
        memcpy(out + 1, name, nl + 1u);
    } else {
        memcpy(out, dir, dl);
        out[dl] = '/';
        memcpy(out + dl + 1u, name, nl + 1u);
    }
    return 0;
}

/** @brief Does an entry exist at dir/name? */
static int
exists(tiku_fs_t *fs, const char *dir, const char *name)
{
    char path[TIKU_PATH_MAX];
    tiku_model_t m;

    if (join(path, sizeof path, dir, name) != 0) {
        return 1;                    /* unusable: treat as taken */
    }
    return (fs->backend->ops->stat(fs->backend, path, &m) == 0);
}

/** @brief The directory part of a path, into @p out. */
static void
parent_of(const char *path, char *out, size_t max)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL || slash == path) {
        snprintf(out, max, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n >= max) { n = max - 1u; }
    memcpy(out, path, n);
    out[n] = '\0';
}

static const char *
leaf_of(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

/**
 * @brief Split "<base> copy N" into its base and the number.
 *
 * This is what stops "x copy" duplicating into "x copy copy": the suffix is
 * recognised and the counter advanced instead (FS-006).
 */
static int
split_suffix(const char *name, const char *word, char *base, size_t max)
{
    size_t nl = strlen(name), wl = strlen(word);
    const char *p;
    int n = 0;

    snprintf(base, max, "%s", name);
    if (nl <= wl + 1u) {
        return 0;
    }
    /* "<base> <word>" or "<base> <word> <n>" */
    p = name + nl;
    while (p > name && p[-1] >= '0' && p[-1] <= '9') {
        p--;
    }
    if (p < name + nl) {
        n = atoi(p);
        if (p > name && p[-1] == ' ') {
            p--;
        }
    }
    if ((size_t)(p - name) > wl && strncmp(p - wl, word, wl) == 0 &&
        p - wl - 1 >= name && p[-wl - 1] == ' ') {
        size_t bl = (size_t)(p - wl - 1 - name);
        if (bl >= max) { bl = max - 1u; }
        memcpy(base, name, bl);
        base[bl] = '\0';
        return (n > 0) ? n : 1;
    }
    return 0;
}

/** @brief First free "<base> <word> [n]" in @p dir. */
static int
free_suffixed_name(tiku_fs_t *fs, const char *dir, const char *name,
                   const char *word, int plain_first, char *out, size_t max)
{
    char base[TIKU_NAME_MAX];
    int n, i;

    n = split_suffix(name, word, base, sizeof base);
    if (plain_first && n == 0 && !exists(fs, dir, name)) {
        snprintf(out, max, "%s", name);   /* the plain name is free */
        return 0;
    }
    if (n == 0) {
        snprintf(out, max, "%s %s", base, word);
        if (!exists(fs, dir, out)) {
            return 0;
        }
        n = 1;
    }
    for (i = (n > 1) ? n : 2; i < 10000; i++) {
        snprintf(out, max, "%s %s %d", base, word, i);
        if (!exists(fs, dir, out)) {
            return 0;
        }
    }
    return -1;
}

int
tiku_fs_duplicate_name(tiku_fs_t *fs, const char *dir,
                           const char *name, char *out, size_t max)
{
    /* Duplicate never reuses the plain name: a duplicate is always "copy". */
    return free_suffixed_name(fs, dir, name, "copy", 0, out, max);
}

int
tiku_fs_link_name(tiku_fs_t *fs, const char *dir, const char *name,
                      char *out, size_t max)
{
    /* A link keeps the original name when the destination allows it. */
    return free_suffixed_name(fs, dir, name, "link", 1, out, max);
}

/*---------------------------------------------------------------------------*/
/* drop semantics                                                            */
/*---------------------------------------------------------------------------*/

/** @brief Volume id of a path, or 0 when it cannot be told. */
/**
 * @brief Which store answers for @p path, and what it is called there.
 *
 * Not "which volume" -- that is st_dev's question and is asked elsewhere.
 * This one decides whether the local filesystem calls in this file may be
 * used at all: they may, for a path a POSIX store answers for, and they
 * may not for anything else.
 */
static tiku_backend_t *
store_of(const tiku_fs_t *fs, const char *path, char *local, size_t max)
{
    return tiku_backend_serving(fs->backend, path, local, max);
}

/**
 * @brief Whether a DEVICE answers for @p path, rather than local files.
 *
 * Nearly everything in this file is about a local filesystem -- statvfs,
 * the volumes scan, the Trash resolution, rename(2), opendir -- and each
 * of those gates used to ask the shell's backend, as a whole, whether it
 * was a device.
 *
 * With a namespace in the way that question has no whole answer.  The
 * router carries its ROOT's identity, so with local files at the root
 * every path under /devices/<board> looked local, and the gates opened
 * on paths no local filesystem has: rename(2) on a device node, unlink()
 * on a device node, a Trash offered for a store that has none.
 *
 * So it is asked per PATH, of whatever actually serves it.
 */
static int
on_device(const tiku_fs_t *fs, const char *path)
{
    tiku_backend_t *on = store_of(fs, path, NULL, 0);

    return on != NULL && on->devid[0] != '\0';
}

static unsigned long
volume_of(tiku_fs_t *fs, const char *path)
{
    struct stat st;

    /* A device store is one volume; its identity IS the volume, so a
     * drop inside one is always a move.  Asked of the store serving
     * THIS path -- a namespace has no single answer. */
    if (on_device(fs, path)) {
        return 1;
    }
    if (stat(path, &st) != 0) {
        char dir[TIKU_PATH_MAX];
        parent_of(path, dir, sizeof dir);
        if (stat(dir, &st) != 0) {
            return 0;
        }
    }
    return (unsigned long)st.st_dev;
}

tiku_op_t
tiku_fs_drop_action(tiku_fs_t *fs, const char *src,
                        const char *dst_dir, int force_copy)
{
    unsigned long a, b;

    if (force_copy) {
        return TIKU_OP_COPY;
    }
    a = volume_of(fs, src);
    b = volume_of(fs, dst_dir);
    /* Same volume: a move is a rename and costs nothing.  Different volume:
     * the bytes have to travel, so it is a copy and the original stays. */
    return (a != 0 && a == b) ? TIKU_OP_MOVE : TIKU_OP_COPY;
}

/*---------------------------------------------------------------------------*/
/* the queue                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Release one operation's records. */
static void
undo_op_free(undo_op_t *o)
{
    free(o->rec);
    o->rec = NULL;
    o->n = 0;
    o->op = TIKU_OP_IDLE;
}

/** @brief Empty a stack. */
static void
undo_stack_clear(undo_op_t *stack, int *count)
{
    while (*count > 0) {
        undo_op_free(&stack[--(*count)]);
    }
}

/**
 * @brief Push @p o, dropping the OLDEST when the stack is full.
 *
 * The oldest goes because the recent operations are the ones anybody wants
 * back; a full stack that refused new entries would silently stop recording
 * exactly when the history matters most.
 */
static undo_op_t *
undo_stack_push(undo_op_t *stack, int *count, const undo_op_t *o)
{
    if (*count >= FS_UNDO_MAX) {
        int i;

        undo_op_free(&stack[0]);
        for (i = 1; i < FS_UNDO_MAX; i++) {
            stack[i - 1] = stack[i];
        }
        stack[FS_UNDO_MAX - 1].rec = NULL;
        (*count)--;
    }
    stack[*count] = *o;
    return &stack[(*count)++];
}

/**
 * @brief Begin recording a new operation of kind @p op.
 *
 * Doing anything new makes the redo history unreachable -- it described a
 * world that no longer exists -- so the redo stack goes here.
 */
static void
undo_reset(tiku_fs_t *fs, tiku_op_t op)
{
    undo_op_t fresh;

    memset(&fresh, 0, sizeof fresh);
    fresh.op = op;
    undo_stack_clear(fs->rstack, &fs->rcount);
    (void)undo_stack_push(fs->ustack, &fs->ucount, &fresh);
}

static void
undo_push(tiku_fs_t *fs, const char *from, const char *to, int created)
{
    undo_op_t  *o;
    undo_rec_t *grown, *r;

    if (fs->ucount == 0) {
        return;
    }
    o = &fs->ustack[fs->ucount - 1];
    grown = realloc(o->rec, (size_t)(o->n + 1) * sizeof *o->rec);
    if (grown == NULL) {
        return;                     /* the operation stands; only its
                                     * reversal is lost                  */
    }
    o->rec = grown;
    r = &o->rec[o->n++];
    memset(r, 0, sizeof *r);
    snprintf(r->from, sizeof r->from, "%s", from);
    snprintf(r->to, sizeof r->to, "%s", to ? to : "");
    r->created = created;
}

/**
 * @brief Whether the Trash lives underneath @p path.
 *
 * A prefix test, not a substring one: "/home/u" contains "/home/u/.Trash",
 * and a plain strstr would also match a sibling called "/home/user-.Trash".
 */
static int
contains_trash(tiku_fs_t *fs, const char *path)
{
    char probe[TIKU_PATH_MAX];
    size_t n;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    n = strlen(path);
    while (n > 1u && path[n - 1u] == '/') {
        n--;                            /* a trailing slash is not a level */
    }
    if (n + sizeof "/.Trash" > sizeof probe) {
        return 0;
    }
    memcpy(probe, path, n);
    probe[n] = '\0';
    return exists(fs, probe, ".Trash");
}

static int
start(tiku_fs_t *fs, tiku_op_t op, const char *const *paths, int n,
      const char *dst, tiku_conflict_t policy)
{
    int i;

    if (fs == NULL || tiku_fs_busy(fs)) {
        return -1;
    }
    if (n > FS_QUEUE_MAX) {
        n = FS_QUEUE_MAX;
    }
    fs->prog.error[0] = '\0';
    fs->prog.skipped = 0;
    fs->qcount = 0;
    for (i = 0; i < n; i++) {
        if ((op == TIKU_OP_COPY || op == TIKU_OP_MOVE) &&
            dst != NULL) {
            /* Refused HERE, where every copy and move passes, so no caller
             * can reach the loop with an item that would destroy something
             * it did not name (FS-023, FS-024, FS-025). */
            char why[200];

            if (tiku_fs_check_move(fs, paths[i], dst, why,
                                       sizeof why) != TIKU_REFUSE_NONE) {
                snprintf(fs->prog.error, sizeof fs->prog.error, "%s", why);
                fs->prog.skipped++;
                continue;
            }
        }
        /* The Trash is dropped from any destructive selection, so it can
         * never be trashed or deleted -- and neither can anything that
         * CONTAINS it, since taking the parent takes the Trash with it
         * (FS-043).  Both tests are silent: the row says so. */
        if (op == TIKU_OP_TRASH &&
            (strstr(paths[i], "/.Trash") != NULL ||
             contains_trash(fs, paths[i]))) {
            /* Scoped to the destructive op.  Applied to every queue it also
             * dropped a plain COPY of any folder that happens to hold a
             * Trash -- copying a home directory would quietly do nothing. */
            continue;
        }
        if ((op == TIKU_OP_TRASH || op == TIKU_OP_DELETE) &&
            !on_device(fs, paths[i])) {
            /* And a whole VOLUME, out loud.  The copy/move gate above does
             * not run for these two ops, and a disk trashed as an ordinary
             * directory is a disk emptied: its own Trash is inside it, so
             * the move either fails or copies the volume into itself. */
            tiku_volumes_t vs;

            if (tiku_volumes_scan(&vs) > 0 &&
                tiku_volume_is_root(&vs, paths[i])) {
                snprintf(fs->prog.error, sizeof fs->prog.error,
                         "\"%.40s\" is a disk, not an item: it can be "
                         "unmounted but not moved, copied or deleted.",
                         leaf_of(paths[i]));
                fs->prog.skipped++;
                continue;
            }
        }
        fs->qexpanded[fs->qcount] = 0;
        snprintf(fs->queue[fs->qcount++], TIKU_PATH_MAX, "%s", paths[i]);
    }
    fs->qcursor = 0;
    fs->policy = policy;
    snprintf(fs->dst, sizeof fs->dst, "%s", dst ? dst : "");
    {   /* A refusal recorded while the queue was built is the reason an
         * item is not in it; clearing progress wholesale would throw the
         * only explanation away. */
        char kept[sizeof fs->prog.error];
        int skipped = fs->prog.skipped;

        snprintf(kept, sizeof kept, "%s", fs->prog.error);
        memset(&fs->prog, 0, sizeof fs->prog);
        snprintf(fs->prog.error, sizeof fs->prog.error, "%s", kept);
        fs->prog.skipped = skipped;
    }
    {
        /* Counted before the job starts, because the QUESTION depends on
         * how many collisions there are and asking that after the first
         * one has been answered is too late (FS-017). */
        int k, collisions = 0;

        for (k = 0; dst != NULL && k < fs->qcount; k++) {
            if (exists(fs, dst, leaf_of(fs->queue[k]))) {
                collisions++;
            }
        }
        fs->prog.collisions = collisions;
    }
    {   /* What the VOLUME allows, before a byte moves.  A move off a
         * read-only volume becomes a copy rather than failing, which is
         * the only way to get a file off a disc at all (FS-013); a
         * destination that will not take a write refuses outright
         * (FS-012); and a delete or a move OF a read-only item is
         * refused with the volume named (FS-045). */
        tiku_volumes_t vs;
        char why[200];
        int k;

        /* The volumes machinery is about LOCAL filesystems; the
         * destination decides whether any of it applies. */
        if (!on_device(fs, (dst != NULL) ? dst : paths[0]) &&
            tiku_volumes_scan(&vs) > 0) {
            if (dst != NULL &&
                !tiku_volume_may_write(&vs, dst, 1, why, sizeof why)) {
                snprintf(fs->prog.error, sizeof fs->prog.error,
                         "You can't move or copy items to read-only "
                         "volumes.");
                fs->prog.error_ask = 1;
                fs->prog.error_can_continue = 0;
                fs->qcursor = fs->qcount;
                return -1;
            }
            if (op == TIKU_OP_MOVE) {
                for (k = 0; k < fs->qcount; k++) {
                    if (!tiku_volume_may_write(&vs, fs->queue[k], 1,
                                                   why, sizeof why)) {
                        op = TIKU_OP_COPY;
                        break;
                    }
                }
            } else if (op == TIKU_OP_TRASH || op == TIKU_OP_DELETE) {
                for (k = 0; k < fs->qcount; k++) {
                    if (!tiku_volume_may_write(&vs, fs->queue[k], 1,
                                                   why, sizeof why)) {
                        snprintf(fs->prog.error, sizeof fs->prog.error,
                                 "%s", why);
                        fs->prog.error_ask = 1;
                        fs->prog.error_can_continue = 0;
                        fs->qcursor = fs->qcount;
                        return -1;
                    }
                }
            }
        }
    }
    if (op == TIKU_OP_COPY && dst != NULL &&
        !tiku_fs_in_trash(fs, dst) && !on_device(fs, dst)) {
        /* Summed before a byte moves: a copy that runs out half way leaves
         * the user with a truncated tree AND less space than they started
         * with.  Skipped for the Trash, where a move is what happens, and
         * for a device store, whose free space is not a number this side
         * knows (FS-015). */
        const char *q[FS_QUEUE_MAX];
        int k;
        uint64_t total;

        for (k = 0; k < fs->qcount; k++) {
            q[k] = fs->queue[k];
        }
        total = tiku_fs_items_size(q, fs->qcount);
        if (!tiku_fs_fits(total, tiku_fs_free_bytes(dst))) {
            snprintf(fs->prog.error, sizeof fs->prog.error,
                     "There is not enough free space on the destination "
                     "volume.");
            fs->prog.error_ask = 1;
            fs->prog.error_can_continue = 0;
            fs->qcursor = fs->qcount;
            return -1;
        }
    }
    if (op == TIKU_OP_COPY && dst != NULL &&
        tiku_fs_in_trash(fs, dst)) {
        /* Copying INTO the Trash would leave the original where it was and
         * put a second copy somewhere the user cannot see, so the gesture
         * means the move it looks like (FS-014). */
        op = TIKU_OP_MOVE;
    }
    fs->prog.op = op;
    fs->prog.total = fs->qcount;
    snprintf(fs->prog.to, sizeof fs->prog.to, "%s", dst ? dst : "");
    fs->cancelling = 0;             /* a new job is not the cancelled one */
    undo_reset(fs, op);
    return 0;
}

int
tiku_fs_copy(tiku_fs_t *fs, const char *const *paths, int n,
                 const char *dst_dir, tiku_conflict_t policy)
{
    return start(fs, TIKU_OP_COPY, paths, n, dst_dir, policy);
}

int
tiku_fs_move(tiku_fs_t *fs, const char *const *paths, int n,
                 const char *dst_dir, tiku_conflict_t policy)
{
    return start(fs, TIKU_OP_MOVE, paths, n, dst_dir, policy);
}

int
tiku_fs_duplicate(tiku_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_OP_DUPLICATE, paths, n, NULL,
                 TIKU_CONFLICT_RENAME);
}

int
tiku_fs_trash(tiku_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_OP_TRASH, paths, n, NULL,
                 TIKU_CONFLICT_SKIP);
}


/**
 * @brief Bytes @p path's filesystem has left, or 0 when it will not say.
 */
uint64_t
tiku_fs_free_bytes(const char *path)
{
    struct statvfs v;

    if (path == NULL || statvfs(path, &v) != 0) {
        return 0u;
    }
    return (uint64_t)v.f_bavail * (uint64_t)v.f_frsize;
}

uint64_t
tiku_fs_items_size(const char *const *paths, int n)
{
    uint64_t total = 0u;
    int i;

    for (i = 0; i < n; i++) {
        struct stat st;
        DIR *d;

        if (paths[i] == NULL || lstat(paths[i], &st) != 0) {
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            /* Charged a whole block, because that is what a file costs on
             * the destination however few bytes it holds; the original
             * clamps the block size to [1024, 8192] and assumes 2048 when
             * the filesystem will not say. */
            unsigned long blk = (st.st_blksize > 0)
                                    ? (unsigned long)st.st_blksize : 2048ul;

            if (blk < 1024ul) { blk = 1024ul; }
            if (blk > 8192ul) { blk = 8192ul; }
            total += (uint64_t)st.st_size + (uint64_t)blk;
            continue;
        }
        total += 1024u;             /* one block for the directory itself */
        d = opendir(paths[i]);
        if (d != NULL) {
            struct dirent *e;

            while ((e = readdir(d)) != NULL) {
                char child[TIKU_PATH_MAX];
                const char *one[1];

                if (strcmp(e->d_name, ".") == 0 ||
                    strcmp(e->d_name, "..") == 0) {
                    continue;
                }
                if (join(child, sizeof child, paths[i], e->d_name) != 0) {
                    continue;
                }
                one[0] = child;
                total += tiku_fs_items_size(one, 1);
            }
            (void)closedir(d);
        }
    }
    return total;
}

int
tiku_fs_fits(uint64_t total, uint64_t free_bytes)
{
    /* Four kilobytes of headroom, and >= rather than >: a destination left
     * with exactly nothing is a destination the next write fails on, so
     * the boundary counts as not fitting. */
    if (free_bytes == 0u) {
        return 1;                   /* unknown: not a reason to refuse    */
    }
    return !((total + 4u * 1024u) >= free_bytes);
}

/** @brief Whether @p path is @p dir or something under it. */
static int
under(const char *path, const char *dir)
{
    size_t n;

    if (path == NULL || dir == NULL || dir[0] == '\0') {
        return 0;
    }
    n = strlen(dir);
    while (n > 1u && dir[n - 1u] == '/') {
        n--;
    }
    return (strncmp(path, dir, n) == 0 &&
            (path[n] == '\0' || path[n] == '/'));
}

tiku_confirm_t
tiku_fs_confirm_move(const char *path)
{
    /* The system directories, as this platform spells them.  Moving
     * anything out of one of these breaks the machine, which is why the
     * original makes the confirmation button need a modifier. */
    static const char *system_dirs[] = {
        "/bin", "/boot", "/etc", "/lib", "/lib64", "/sbin", "/usr", "/var"
    };
    const char *home = getenv("HOME");
    size_t i;

    if (path == NULL || path[0] == '\0') {
        return TIKU_CONFIRM_NONE;
    }
    for (i = 0; i < sizeof system_dirs / sizeof system_dirs[0]; i++) {
        if (under(path, system_dirs[i])) {
            return TIKU_CONFIRM_OVERRIDE;
        }
    }
    if (home != NULL && home[0] != '\0') {
        char cfg[TIKU_PATH_MAX];
        const char *xdg = getenv("XDG_CONFIG_HOME");

        /* HOME itself, and ONLY home itself: the original tests the home
         * folder for an exact match where it tests the system folder for
         * containment, so an ordinary file inside home is moved without a
         * word.  Ported as it is -- a browser that asked before every move
         * inside the home folder would train the answer out of the user. */
        if (strcmp(path, home) == 0) {
            return TIKU_CONFIRM_OVERRIDE;
        }
        if (xdg != NULL && xdg[0] != '\0' && under(path, xdg)) {
            return TIKU_CONFIRM_ASK;
        }
        if (snprintf(cfg, sizeof cfg, "%s/.config", home) > 0 &&
            under(path, cfg)) {
            return TIKU_CONFIRM_ASK;
        }
    }
    return TIKU_CONFIRM_NONE;
}

int
tiku_fs_in_trash(tiku_fs_t *fs, const char *path)
{
    char trash[TIKU_PATH_MAX];
    size_t n;

    if (fs == NULL || path == NULL ||
        tiku_fs_trash_dir(fs, path, trash, sizeof trash) != 0) {
        return 0;
    }
    n = strlen(trash);
    /* A prefix test on a whole component, so a sibling named ".Trashcan"
     * is not mistaken for something inside the Trash. */
    return (strncmp(path, trash, n) == 0 &&
            (path[n] == '\0' || path[n] == '/'));
}

int
tiku_fs_link(tiku_fs_t *fs, const char *const *paths, int n,
                 const char *dst, int relative)
{
    if (fs == NULL) {
        return -1;
    }
    fs->link_relative = relative;
    return start(fs, TIKU_OP_LINK, paths, n, dst,
                 TIKU_CONFLICT_ASK);
}

int
tiku_fs_delete(tiku_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_OP_DELETE, paths, n, NULL,
                 TIKU_CONFLICT_ASK);
}
/*---------------------------------------------------------------------------*/
/* the work                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Copy one file's bytes through the backend. */
/**
 * @brief Give @p dst the source's permissions, owner, group and times.
 *
 * A copy that lands as a fresh file owned by whoever ran the browser, world
 * readable and stamped now, is not the same file: mode and mtime are part of
 * what was copied (FS-031).  Ownership is attempted and allowed to fail --
 * only root may give a file away.
 *
 * @param link  Non-zero when @p dst is a symlink, which must not be followed.
 */
static void
carry_over(const char *src, const char *dst, int link)
{
    struct stat st;
    struct timespec times[2];

    if ((link ? lstat(src, &st) : stat(src, &st)) != 0) {
        return;
    }
    times[0] = st.st_atim;
    times[1] = st.st_mtim;
    if (link) {
        /* Only root may give a file away, so a refusal here is the normal
         * case and not a failure of the copy. */
        if (lchown(dst, st.st_uid, st.st_gid) != 0) { errno = 0; }
        (void)utimensat(AT_FDCWD, dst, times, AT_SYMLINK_NOFOLLOW);
        return;
    }
    if (chown(dst, st.st_uid, st.st_gid) != 0) { errno = 0; }
    (void)chmod(dst, st.st_mode & 07777u);
    (void)utimensat(AT_FDCWD, dst, times, 0);
}

/**
 * @brief Carry an icon pose record along with a cross-folder copy.
 *
 * Pose information is per item in the state store, so copying the bytes is
 * the backend equivalent of Tracker's kAttrPoseInfo inheritance.  A later
 * explicit drop point still wins when the destination view receives the
 * create notice; this only supplies the useful default when no point was
 * specified (FS-037).
 */
static void
copy_pose_info(tiku_fs_t *fs, const char *src, const char *dst)
{
    tiku_store_t *store;
    unsigned char raw[64];
    int n;

    if (fs == NULL || fs->backend == NULL || src == NULL || dst == NULL ||
        fs->backend->ops->state_store == NULL) {
        return;
    }
    store = fs->backend->ops->state_store(fs->backend);
    if (store == NULL || !tiku_store_writable(store)) {
        return;
    }
    n = tiku_state_read(store, src, TIKU_ATTR_POSE_INFO, raw,
                            sizeof raw);
    if (n > 0) {
        (void)tiku_state_write(store, dst, TIKU_ATTR_POSE_INFO, raw,
                                   (size_t)n);
    }
}

/**
 * @brief Copy one node from one store to another, through both of them.
 *
 * The only road between two stores is the backend contract, and it is
 * narrow in two ways that decide what this can promise.
 *
 * Its read takes no OFFSET: a node's value comes back whole, into
 * whatever buffer is offered.  So this carries what one buffer holds and
 * REFUSES what it does not, because a copy that silently kept the first
 * sixty-four kilobytes of a file would be worse than one that did not
 * happen.  Lifting that means an offset in the ops table, which is a
 * change to the seam rather than to this.
 *
 * And it has no way to make a directory or take one away -- write is the
 * whole of what it can change -- so a FOLDER crossing between stores is
 * refused rather than half-built.
 */
static int
copy_across(tiku_fs_t *fs, tiku_backend_t *sb, const char *slocal,
            tiku_backend_t *db, const char *dlocal, const char *src,
            char *err, size_t errmax)
{
    static unsigned char across[64 * 1024];
    tiku_model_t m;
    int len;

    /* Asked once and up front: what follows is one read and one write,
     * so there is no middle to stop in. */
    if (cancelled(fs)) {
        snprintf(err, errmax, "cancelled");
        return -2;
    }
    if (sb->ops->stat(sb, slocal, &m) != 0) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    if (tiku_model_is_container(&m)) {
        snprintf(err, errmax,
                 "a folder cannot be copied between stores yet: the "
                 "backend contract has no way to make one");
        return -1;
    }
    if (m.facts.size > (int64_t)sizeof across) {
        snprintf(err, errmax,
                 "'%.40s' is too big to copy between stores: they are "
                 "read whole, and this one is %lld bytes",
                 leaf_of(src), (long long)m.facts.size);
        return -1;
    }
    len = sb->ops->read(sb, slocal, across, sizeof across);
    if (len < 0) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    /*
     * A short read where the entry said it was longer is a truncated
     * copy waiting to happen, and the caller would never know: the write
     * would succeed and the file would simply be wrong.
     */
    if (m.facts.size > 0 && (int64_t)len < m.facts.size) {
        snprintf(err, errmax,
                 "'%.40s' was read short, %d of %lld bytes", leaf_of(src),
                 len, (long long)m.facts.size);
        return -1;
    }
    if (db->ops->write == NULL) {
        snprintf(err, errmax, "that store does not take writes");
        return -1;
    }
    return db->ops->write(db, dlocal, across, (size_t)len, err, errmax);
}

static int
copy_bytes(tiku_fs_t *fs, const char *src, const char *dst, char *err,
           size_t errmax)
{
    static unsigned char buf[64 * 1024];
    FILE *in, *out;
    size_t n;

    /* Local files copy directly; a device file goes through the backend
     * so its own transfer discipline applies.  Both ends are on ONE store
     * here -- a crossing was taken above -- so the source answers for
     * both. */
    if (on_device(fs, src)) {
        int len = fs->backend->ops->read(fs->backend, src, buf, sizeof buf);
        if (len < 0) {
            snprintf(err, errmax, "cannot read '%s'", src);
            return -1;
        }
        return fs->backend->ops->write(fs->backend, dst, buf, (size_t)len,
                                       err, errmax);
    }
    {   /* Re-checked per file, not only up front: another process may
         * have taken the space between the two, and finding out at the
         * write is finding out too late (FS-016). */
        struct stat st;
        char dir[TIKU_PATH_MAX];

        uint64_t freeb;

        parent_of(dst, dir, sizeof dir);
        freeb = tiku_fs_free_bytes(dir);
        /* One kilobyte of headroom here, not the four the whole job was
         * checked against: this is the last look before the write. */
        if (freeb != 0u && stat(src, &st) == 0 &&
            (uint64_t)st.st_size + 1024u >= freeb) {
            snprintf(err, errmax, "There is not enough free space on the "
                                  "destination volume.");
            return -1;
        }
    }
    in = fopen(src, "rb");
    if (in == NULL) {
        snprintf(err, errmax, "cannot read '%s' (%s)", src, strerror(errno));
        return -1;
    }
    out = fopen(dst, "wb");
    if (out == NULL) {
        (void)fclose(in);
        snprintf(err, errmax, "cannot create '%s' (%s)", dst,
                 strerror(errno));
        return -1;
    }
    for (;;) {
        /* Before the chunk, not after: a cancel that had to wait for the
         * write it was trying to stop would not be a cancel. */
        if (cancelled(fs)) {
            (void)fclose(in);
            (void)fclose(out);
            /* The destination is half a file and nothing owns it: it was
             * created by this copy and the copy is not happening.  Leaving
             * it would put a truncated file under the real one's name
             * (FS-033). */
            (void)remove(dst);
            snprintf(err, errmax, "cancelled");
            return -2;
        }
        n = fread(buf, 1, sizeof buf, in);
        if (n == 0) {
            break;
        }
        fs->prog.bytes += (int64_t)n;
        if (fwrite(buf, 1, n, out) != n) {
            (void)fclose(in);
            (void)fclose(out);
            snprintf(err, errmax, "write failed on '%s'", dst);
            return -1;
        }
    }
    (void)fclose(in);
    return (fclose(out) == 0) ? 0 : -1;
}

/** @brief Process one queued item.  @return 1 done, 0 stopped on a conflict. */



/*---------------------------------------------------------------------------*/
/* Copying and moving a tree (FS-020, FS-029)                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Remove @p path and everything under it.
 *
 * Best effort, as the original's delete loop is: one child that will not go
 * does not abandon the rest, because leaving a half-deleted tree AND
 * reporting failure is worse than removing what can be removed.
 *
 * @return 0 when the top-level item went.
 */
static int
remove_tree(const char *path)
{
    struct stat st;
    DIR *d;
    struct dirent *e;

    if (stat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return (unlink(path) == 0) ? 0 : -1;
    }
    d = opendir(path);
    while (d != NULL && (e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (join(child, sizeof child, path, e->d_name) == 0) {
            (void)remove_tree(child);
        }
    }
    if (d != NULL) {
        (void)closedir(d);
    }
    return (rmdir(path) == 0) ? 0 : -1;
}

/**
 * @brief Copy @p src into @p dst_dir, merging into a folder already there.
 *
 * "Replacing" a folder is a MERGE and never a delete-then-copy: the
 * destination folder is kept, its children that have no counterpart in the
 * source SURVIVE, same-named files are replaced, and same-named folders
 * recurse into the same rule.  Deleting the destination first would throw
 * away everything the user did not name.
 *
 * @return 0 on success.
 */
static int
copy_tree(tiku_fs_t *fs, const char *src, const char *dstpath, char *err,
          size_t errmax)
{
    struct stat st;
    dev_t sdev;
    DIR *d;
    struct dirent *e;
    {
        /*
         * Two different stores, so none of the local filesystem calls
         * below apply: lstat would be asking this host about a path that
         * lives on a board.  The backend contract is the only road, and
         * copy_across says what it can and cannot carry.
         */
        char sl[TIKU_PATH_MAX], dl[TIKU_PATH_MAX];
        tiku_backend_t *sb = store_of(fs, src, sl, sizeof sl);
        tiku_backend_t *db = store_of(fs, dstpath, dl, sizeof dl);

        if (sb != NULL && db != NULL && sb != db) {
            return copy_across(fs, sb, sl, db, dl, src, err, errmax);
        }
    }

    /* The full destination PATH, not the folder: the name may have been
     * chosen by a collision answer, and recomputing it from the source
     * leaf would quietly undo that choice. */
    /* lstat, so a LINK is seen as a link: stat() follows it and would
     * report whatever it points at, which is how a link gets copied as a
     * duplicate of its target (FS-032). */
    if (lstat(src, &st) != 0) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    if (S_ISLNK(st.st_mode)) {
        /* The LINK, not what it points at.  Following it would turn a
         * one-line pointer into a copy of a whole tree, and a link into a
         * device node into a snapshot of one moment of it (FS-032). */
        char target[TIKU_PATH_MAX];
        ssize_t n = readlink(src, target, sizeof target - 1u);

        if (n < 0) {
            snprintf(err, errmax, "cannot read the link '%.50s'",
                     leaf_of(src));
            return -1;
        }
        target[n] = '\0';
        (void)remove(dstpath);
        if (symlink(target, dstpath) != 0) {
            snprintf(err, errmax, "cannot create the link '%.50s' (%.40s)",
                     leaf_of(dstpath), strerror(errno));
            return -1;
        }
        /* Permissions and times, but no attributes: the original does not
         * copy those for a link either. */
        carry_over(src, dstpath, 1);
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        int rc = copy_bytes(fs, src, dstpath, err, errmax);

        if (rc == 0) {
            carry_over(src, dstpath, 0);
        }
        return rc;
    }
    /* Held before `st` is re-used for the destination, because the
     * mount-point test below compares each child against the SOURCE's
     * filesystem and would otherwise compare against the wrong one. */
    sdev = st.st_dev;
    if (stat(dstpath, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            snprintf(err, errmax,
                     "cannot replace the file '%.50s' with a folder",
                     leaf_of(dstpath));
            return -1;
        }
        /* It is there and it is a folder: merge INTO it.  Its own
         * attributes are left alone for the same reason its children are --
         * the user asked for the contents to arrive, not for the folder to
         * be replaced by another one. */
    } else if (mkdir(dstpath, 0755) != 0) {
        snprintf(err, errmax, "cannot create '%.60s' (%.40s)",
                 leaf_of(dstpath), strerror(errno));
        return -1;
    } else {
        carry_over(src, dstpath, 0);
    }
    d = opendir(src);
    if (d == NULL) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        {
            char cdst[TIKU_PATH_MAX];

            if (join(child, sizeof child, src, e->d_name) != 0 ||
                join(cdst, sizeof cdst, dstpath, e->d_name) != 0) {
                continue;
            }
            /* A failure on one child does not abandon the rest: the loop
             * is best-effort, as the original's is, so one unreadable file
             * does not cost the user the whole folder. */
            {   /* A directory on a DIFFERENT filesystem is a mount
                 * point: descending into it would copy a whole other
                 * volume that the user pointed at nothing of.  And an
                 * entry that is neither file, folder nor link is
                 * ignored -- a device node has no bytes to copy (FS-036). */
                struct stat cs;

                if (lstat(child, &cs) == 0) {
                    if (S_ISDIR(cs.st_mode) && cs.st_dev != sdev) {
                        continue;
                    }
                    if (!S_ISDIR(cs.st_mode) && !S_ISREG(cs.st_mode) &&
                        !S_ISLNK(cs.st_mode)) {
                        continue;
                    }
                }
            }
            if (copy_tree(fs, child, cdst, err, errmax) == -2) {
                /* Cancelled.  The folder already made and the children
                 * already written STAY: only the file being written when
                 * the cancel arrived is cleaned up, so what the user sees
                 * is how far the copy got rather than nothing (FS-034). */
                (void)closedir(d);
                return -2;
            }
        }
    }
    (void)closedir(d);
    return 0;
}

/**
 * @brief Move @p src into @p dst_dir on one filesystem, merging (FS-029).
 *
 * A same-volume move never copies.  Where the destination already holds a
 * folder of the same name it is descended into and the emptied source
 * folders are removed behind it, which is a merge rather than a refusal.
 *
 * @return 0 on success.
 */
static int
move_tree(tiku_fs_t *fs, const char *src, const char *dstpath, char *err,
          size_t errmax)
{
    struct stat sst, dst_st;
    DIR *d;
    struct dirent *e;
    {
        /*
         * Between two stores a move is a copy and then a delete, which is
         * what it already is across a volume boundary -- but the delete
         * can only be done where the local filesystem answers, because
         * the backend contract has no way to take anything away.  So a
         * move OFF a device is refused rather than left as a copy the
         * user believes was a move.
         */
        char sl[TIKU_PATH_MAX], dl[TIKU_PATH_MAX];
        tiku_backend_t *sb = store_of(fs, src, sl, sizeof sl);
        tiku_backend_t *db = store_of(fs, dstpath, dl, sizeof dl);

        if (sb != NULL && db != NULL && sb != db) {
            int rc;

            if (sb->devid[0] != '\0') {
                snprintf(err, errmax,
                         "'%.40s' can be copied off that store but not "
                         "moved: nothing in the backend contract removes "
                         "anything", leaf_of(src));
                return -1;
            }
            rc = copy_across(fs, sb, sl, db, dl, src, err, errmax);
            if (rc != 0) {
                return rc;
            }
            return remove_tree(sl);
        }
    }

    if (stat(src, &sst) != 0) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    if (stat(dstpath, &dst_st) != 0) {
        /* Nothing in the way: one rename moves the whole tree. */
        if (rename(src, dstpath) == 0) {
            return 0;
        }
        if (errno != EXDEV) {
            snprintf(err, errmax, "cannot move '%.60s' (%.40s)",
                     leaf_of(src), strerror(errno));
            return -1;
        }
        /* Across a boundary a move is a copy followed by a delete, which
         * is the one case where a move reads every byte (FS-030). */
        if (copy_tree(fs, src, dstpath, err, errmax) != 0) {
            return -1;
        }
        return remove_tree(src);
    }
    if (!S_ISDIR(sst.st_mode) || !S_ISDIR(dst_st.st_mode)) {
        /* A file over a file: the refusal gate has already allowed this,
         * so the destination goes and the source takes its place. */
        (void)remove(dstpath);
        if (rename(src, dstpath) != 0) {
            snprintf(err, errmax, "cannot move '%.60s' (%.40s)",
                     leaf_of(src), strerror(errno));
            return -1;
        }
        return 0;
    }
    d = opendir(src);
    if (d == NULL) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        {
            char cdst[TIKU_PATH_MAX];

            if (join(child, sizeof child, src, e->d_name) == 0 &&
                join(cdst, sizeof cdst, dstpath, e->d_name) == 0) {
                (void)move_tree(fs, child, cdst, err, errmax);
            }
        }
    }
    (void)closedir(d);
    /* The source folder is left behind only if something in it would not
     * move; an empty one goes, which is what makes this a move. */
    (void)rmdir(src);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Refusals (FS-023, FS-024, FS-025)                                         */
/*---------------------------------------------------------------------------*/

/** @brief Whether @p dir is @p path or contains it, at a component boundary. */
static int
path_contains(const char *dir, const char *path)
{
    size_t n;

    if (dir == NULL || path == NULL) {
        return 0;
    }
    n = strlen(dir);
    while (n > 1u && dir[n - 1u] == '/') {
        n--;
    }
    if (strncmp(dir, path, n) != 0) {
        return 0;
    }
    /* The same path counts as contained; a longer one only when the next
     * character is a separator, or "/a/b" would swallow "/a/bc". */
    return path[n] == '\0' || path[n] == '/';
}

/** @brief Whether @p path is a directory. */
static int
is_dir(const char *path)
{
    struct stat st;

    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

tiku_refusal_t
tiku_fs_check_move(tiku_fs_t *fs, const char *src, const char *dst_dir,
                       char *msg, size_t max)
{
    char dstpath[TIKU_PATH_MAX];

    if (msg != NULL && max > 0u) {
        msg[0] = '\0';
    }
    if (fs == NULL || src == NULL || dst_dir == NULL) {
        return TIKU_REFUSE_NONE;
    }
    /* The Trash is not an item to be moved about; it is where items go. */
    if (strstr(src, "/" TIKU_TRASH_NAME) != NULL &&
        strcmp(leaf_of(src), TIKU_TRASH_NAME) == 0) {
        if (msg != NULL) {
            snprintf(msg, max, "You can't move or copy the trash.");
        }
        return TIKU_REFUSE_TRASH;
    }
    /*
     * A whole VOLUME is not an item.  Refused here, at the one gate every
     * copy and move passes, because the alternative is not a failure: a
     * mount point moved off its own device gets rename(EXDEV), and the
     * fallback copies every byte of the volume and then deletes what it
     * copied from.  Dragging a disk to the Trash must not empty the disk.
     */
    if (!on_device(fs, src)) {
        tiku_volumes_t vs;

        if (tiku_volumes_scan(&vs) > 0 &&
            tiku_volume_is_root(&vs, src)) {
            if (msg != NULL) {
                /* What the gesture MEANS is unmount, which this port
                 * cannot do -- so it says what it cannot do rather than
                 * doing something else. */
                snprintf(msg, max,
                         "\"%.40s\" is a disk, not an item: it can be "
                         "unmounted but not moved, copied or deleted.",
                         leaf_of(src));
            }
            return TIKU_REFUSE_VOLUME;
        }
    }
    /* Into ITSELF, or into anything underneath it.  Tested on the source
     * directory, because that is the thing that would end up inside a copy
     * of itself, for ever. */
    if (is_dir(src) && path_contains(src, dst_dir)) {
        if (msg != NULL) {
            snprintf(msg, max,
                     "You can't move a folder into itself or any of its own "
                     "sub-folders.");
        }
        return TIKU_REFUSE_INTO_SELF;
    }
    if (join(dstpath, sizeof dstpath, dst_dir, leaf_of(src)) != 0) {
        return TIKU_REFUSE_NONE;
    }
    {
        struct stat dst_st;

        if (stat(dstpath, &dst_st) != 0) {
            return TIKU_REFUSE_NONE;    /* no collision, no question  */
        }
        /* The second refusal is the first one inverted: here the thing
         * being overwritten is an ancestor of the thing overwriting it. */
        if (S_ISDIR(dst_st.st_mode) && path_contains(dstpath, src)) {
            if (msg != NULL) {
                snprintf(msg, max,
                         "You can't replace a folder with one of its "
                         "sub-folders.");
            }
            return TIKU_REFUSE_BY_CHILD;
        }
        /* A kind mismatch is refused outright -- there is no "replace
         * anyway", because neither thing can stand in for the other. */
        if (S_ISDIR(dst_st.st_mode) != (is_dir(src) ? 1 : 0)) {
            if (msg != NULL) {
                snprintf(msg, max, is_dir(src)
                         ? "You cannot replace a file with a folder or a "
                           "symbolic link."
                         : "You cannot replace a folder or a symbolic link "
                           "with a file.");
            }
            return TIKU_REFUSE_KIND;
        }
    }
    return TIKU_REFUSE_NONE;
}

/*---------------------------------------------------------------------------*/
/* The Trash (FS-038, FS-039, FS-040, FS-044, FS-047, FS-048)                */
/*---------------------------------------------------------------------------*/

/** @brief Create @p path and every missing folder above it. */
static int
make_path(const char *path)
{
    char buf[TIKU_PATH_MAX];
    char *p;

    snprintf(buf, sizeof buf, "%s", path);
    for (p = buf + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        (void)mkdir(buf, 0755);
        *p = '/';
    }
    return mkdir(buf, 0755);
}

int
tiku_fs_has_trash(const tiku_fs_t *fs)
{
    /*
     * A device store has nowhere to put anything: its nodes are not
     * files.  Saying so is what lets the caller offer an honest "delete
     * outright" rather than a Trash that silently destroys (FS-044).
     *
     * This one is about the namespace as a WHOLE -- it takes no path, so
     * it cannot be about anything else -- and it answers for the root,
     * which is where a Trash would be.  Whether a particular ITEM can be
     * trashed is a different question with a different answer, and it is
     * asked where a path exists: tiku_fs_trash_dir() below.
     */
    return (fs != NULL && fs->backend != NULL && !on_device(fs, "/"));
}

/**
 * @brief The root of the filesystem @p path lives on.
 *
 * Walked up until the device changes, which is where that filesystem is
 * mounted.  The Trash has to be on the item's OWN filesystem or trashing
 * stops being a rename and becomes a copy across a boundary -- which is
 * slow, can half-fail, and on this host simply returns EXDEV.
 */
static void
volume_root(const char *path, char *out, size_t max)
{
    char cur[TIKU_PATH_MAX], parent[TIKU_PATH_MAX];
    struct stat st, pst;

    snprintf(out, max, "/");
    if (path == NULL || path[0] == '\0') {
        return;
    }
    snprintf(cur, sizeof cur, "%s", path);
    /* The item itself may be gone -- asking where a trashed thing CAME
     * from is exactly this case -- so start from the nearest ancestor that
     * still exists.  Tracker keeps the device in the node ref and never
     * has to ask twice; a path is all this store has. */
    while (stat(cur, &st) != 0) {
        char *slash = strrchr(cur, '/');

        if (slash == NULL || slash == cur) {
            return;                     /* nothing above it exists       */
        }
        *slash = '\0';
    }
    for (;;) {
        char *slash = strrchr(cur, '/');

        if (slash == NULL) {
            break;
        }
        if (slash == cur) {
            snprintf(parent, sizeof parent, "/");
        } else {
            snprintf(parent, sizeof parent, "%.*s", (int)(slash - cur), cur);
        }
        if (stat(parent, &pst) != 0 || pst.st_dev != st.st_dev) {
            snprintf(out, max, "%s", cur);   /* the parent is elsewhere   */
            return;
        }
        if (strcmp(parent, "/") == 0) {
            snprintf(out, max, "/");
            return;
        }
        snprintf(cur, sizeof cur, "%s", parent);
    }
}

/** @brief Make @p dir/.Trash if it is not there.  @return 0 on success. */
static int
trash_under(const char *dir, char *out, size_t max)
{
    struct stat st;
    int n;

    /* One slash: "/" and "/home" both have to join cleanly, and "//.Trash"
     * is a different path from the one anything else will look in. */
    n = snprintf(out, max, "%s%s%s", dir,
                 (dir[0] != '\0' && dir[strlen(dir) - 1u] == '/') ? "" : "/",
                 TIKU_TRASH_NAME);
    if (n < 0 || (size_t)n >= max) {
        return -1;
    }
    if (stat(out, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return (mkdir(out, 0755) == 0) ? 0 : -1;
}

int
tiku_fs_trash_dirs(tiku_fs_t *fs, char out[][TIKU_PATH_MAX],
                       int max)
{
    tiku_volumes_t vs;

    if (fs == NULL || out == NULL || max <= 0 ||
        !tiku_fs_has_trash(fs)) {
        return 0;
    }
    if (on_device(fs, "/") || tiku_volumes_scan(&vs) <= 0) {
        /* A device namespace has one Trash at most, and it is wherever the
         * item's own resolution puts it. */
        if (tiku_fs_trash_dir(fs, NULL, out[0], TIKU_PATH_MAX) ==
                0) {
            return 1;
        }
        return 0;
    }
    return tiku_fs_trash_dirs_of(fs,
               (const struct tiku_volumes *)&vs, out, max);
}

int
tiku_fs_prepare_trash(tiku_fs_t *fs)
{
    char dirs[TIKU_VOLUME_MAX][TIKU_PATH_MAX];

    if (fs == NULL) {
        return 0;
    }
    /* The directory pass is deliberately explicit at application startup;
     * constructing an operation engine must remain side-effect free for
     * callers such as tests and file panels. */
    return tiku_fs_trash_dirs(fs, dirs, TIKU_VOLUME_MAX);
}

int
tiku_fs_trash_dirs_of(tiku_fs_t *fs, const struct tiku_volumes *v,
                          char out[][TIKU_PATH_MAX], int max)
{
    const tiku_volumes_t *vsp = (const tiku_volumes_t *)v;
    int i, n = 0;

    if (fs == NULL || vsp == NULL || out == NULL || max <= 0) {
        return 0;
    }
    for (i = 0; i < vsp->n && n < max; i++) {
        char here[TIKU_PATH_MAX];
        int k, dupe = 0;

        if (vsp->v[i].read_only) {
            /* Nothing can be trashed onto it, so it has no Trash -- and
             * asking would try to create a directory on a disc. */
            continue;
        }
        if (tiku_fs_trash_dir(fs, vsp->v[i].mount, here,
                                  sizeof here) != 0) {
            continue;
        }
        for (k = 0; k < n; k++) {
            if (strcmp(out[k], here) == 0) {
                dupe = 1;       /* two mounts resolving to one Trash */
                break;
            }
        }
        if (!dupe) {
            snprintf(out[n++], TIKU_PATH_MAX, "%s", here);
        }
    }
    return n;
}

int
tiku_fs_trash_dir(tiku_fs_t *fs, const char *path, char *out,
                      size_t max)
{
    char root[TIKU_PATH_MAX];
    const char *home = getenv("HOME");
    struct stat rs, hs;

    if (fs == NULL || out == NULL || path == NULL ||
        !tiku_fs_has_trash(fs) || on_device(fs, path)) {
        /* Per ITEM, not per namespace: with a board mounted beside the
         * disk, the disk has a Trash and the board has none, and the
         * question is which of them this path is on. */
        return -1;
    }
    /* Resolved from the ITEM's own filesystem, as Tracker resolves it from
     * the item's own device: a Trash somewhere else turns every trash into
     * a cross-boundary copy, which is exactly what this rule prevents. */
    volume_root((path != NULL) ? path : home, root, sizeof root);
    /* The home directory first, whenever it shares that filesystem.  A
     * volume root can be writable by EVERYONE -- a sticky /tmp is one --
     * and a Trash made there belongs to whoever trashed first; every
     * other user's trashing then fails on a neighbour's directory.  The
     * home directory is the per-user writable place on the same
     * filesystem, which keeps the move a rename AND keeps the Trash
     * ours. */
    if (home != NULL && home[0] != '\0' && stat(home, &hs) == 0 &&
        stat(root, &rs) == 0 && hs.st_dev == rs.st_dev &&
        trash_under(home, out, max) == 0) {
        return 0;
    }
    /* A volume the home does not live on keeps its Trash at its root. */
    return (trash_under(root, out, max) == 0) ? 0 : -1;
}

int
tiku_fs_original_path(tiku_fs_t *fs, const char *path, char *out,
                          size_t max)
{
    tiku_store_t *store;
    int n;

    if (fs == NULL || path == NULL || out == NULL || max == 0u ||
        fs->backend->ops->state_store == NULL) {
        return 0;
    }
    store = fs->backend->ops->state_store(fs->backend);
    if (store == NULL) {
        return 0;
    }
    n = tiku_state_read(store, path, TIKU_ATTR_ORIGINAL, out,
                            max - 1u);
    if (n <= 0) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return 1;
}

/** @brief Remember where @p trashed came from, under its trashed name. */
static void
record_origin(tiku_fs_t *fs, const char *trashed, const char *origin)
{
    tiku_store_t *store;

    if (fs->backend->ops->state_store == NULL) {
        return;
    }
    store = fs->backend->ops->state_store(fs->backend);
    if (store == NULL || !tiku_store_writable(store)) {
        return;
    }
    /* Recorded against the path the item now HAS.  Tracker writes the
     * attribute on the node, where it travels with the file; a store keyed
     * by path has to write it where the item ended up, or the record is
     * filed under a name nothing answers to. */
    (void)tiku_state_write(store, trashed, TIKU_ATTR_ORIGINAL,
                               origin, strlen(origin));
    (void)tiku_store_flush(store);
}

tiku_delete_q_t
tiku_fs_delete_question(const tiku_fs_t *fs, const char *path,
                            int shift_held)
{
    if (fs == NULL) {
        return TIKU_DELETE_STRAIGHT;
    }
    if (shift_held) {
        return TIKU_DELETE_STRAIGHT;    /* the user has decided       */
    }
    {
        /* Asked of THIS item's volume, not of the store: a filesystem with
         * no writable Trash cannot offer to move anything to one, and the
         * three-way question would show a button that does nothing
         * (FS-044). */
        char trash[TIKU_PATH_MAX];

        if (tiku_fs_trash_dir((tiku_fs_t *)fs, path, trash,
                                  sizeof trash) != 0) {
            return TIKU_DELETE_STRAIGHT;
        }
    }
    if (path != NULL && strstr(path, "/" TIKU_TRASH_NAME) != NULL) {
        /* Inside the Trash the only thing left IS deletion, so asking
         * whether to move it to the Trash asks nothing (FS-047). */
        return TIKU_DELETE_STRAIGHT;
    }
    return TIKU_DELETE_ASK;
}

int
tiku_fs_restore(tiku_fs_t *fs, const char *const *paths, int n,
                    char *err, size_t errmax)
{
    int i, done = 0;

    if (fs == NULL || paths == NULL) {
        return 0;
    }
    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    for (i = 0; i < n; i++) {
        char origin[TIKU_PATH_MAX];
        struct stat st;

        if (!tiku_fs_original_path(fs, paths[i], origin, sizeof origin)) {
            if (err != NULL) {
                snprintf(err, errmax,
                         "'%s' does not remember where it came from",
                         leaf_of(paths[i]));
            }
            continue;
        }
        if (stat(origin, &st) == 0) {
            /* Refused, not overwritten: putting one item back is not
             * permission to destroy whatever took its place (FS-040). */
            if (err != NULL) {
                snprintf(err, errmax,
                         "cannot restore '%s': something is there already",
                         leaf_of(origin));
            }
            continue;
        }
        {   /* The folders it lived in may have gone since; make them. */
            char parent[TIKU_PATH_MAX];
            char *slash;

            snprintf(parent, sizeof parent, "%s", origin);
            slash = strrchr(parent, '/');
            if (slash != NULL && slash != parent) {
                *slash = '\0';
                if (stat(parent, &st) != 0) {
                    (void)make_path(parent);
                }
            }
        }
        if (rename(paths[i], origin) != 0) {
            if (err != NULL) {
                snprintf(err, errmax, "cannot restore '%s' (%s)",
                         leaf_of(paths[i]), strerror(errno));
            }
            continue;
        }
        landed(fs, paths[i], origin);
        done++;
    }
    return done;
}

int
tiku_fs_empty_trash(tiku_fs_t *fs)
{
    char dirs[TIKU_VOLUME_MAX][TIKU_PATH_MAX];
    char paths[FS_QUEUE_MAX][TIKU_PATH_MAX];
    const char *sel[FS_QUEUE_MAX];
    int n = 0;
    int nd, k;

    if (fs == NULL) {
        return 0;
    }
    /* EVERY volume's trash at once (FS-048), the same set the union
     * window lists (PVN-027) -- read-only and unreachable volumes simply
     * contribute no directory.  The Trash folders themselves stay: only
     * what is IN them is queued. */
    nd = tiku_fs_trash_dirs(fs, dirs, TIKU_VOLUME_MAX);
    for (k = 0; k < nd; k++) {
        DIR *d = opendir(dirs[k]);
        struct dirent *e;

        if (d == NULL) {
            continue;
        }
        while ((e = readdir(d)) != NULL && n < FS_QUEUE_MAX) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }
            if ((int)snprintf(paths[n], sizeof paths[n], "%s/%s", dirs[k],
                              e->d_name) >= (int)sizeof paths[n]) {
                continue;               /* a name that cannot be addressed */
            }
            sel[n] = paths[n];
            n++;
        }
        (void)closedir(d);
    }
    if (n == 0) {
        return 0;
    }
    /* DELETE, not trash: the queue builder refuses to trash anything
     * inside the Trash -- rightly, since the Trash cannot be put in
     * itself -- and emptying it is the other rule (FS-043 vs FS-048). */
    (void)tiku_fs_delete(fs, sel, n);
    return n;
}

/**
 * @brief Write the path of @p target as seen from inside @p dir.
 *
 * Built by walking the two absolute paths while they agree, then one ".."
 * per component of @p dir that is left, then the rest of the target.  The
 * original chdir()s to do this; a string walk gets the same answer without
 * moving the process's working directory out from under everything else.
 *
 * @return 0 on success.
 */
static int
relative_path(const char *dir, const char *target, char *out, size_t max)
{
    size_t i = 0, cut = 0;
    size_t n = 0;

    if (dir == NULL || target == NULL || dir[0] != '/' || target[0] != '/') {
        return -1;
    }
    while (dir[i] != '\0' && target[i] != '\0' && dir[i] == target[i]) {
        if (dir[i] == '/') {
            cut = i;                /* last slash they still share */
        }
        i++;
    }
    if (dir[i] == '\0' && (target[i] == '/' || target[i] == '\0')) {
        cut = i;
    }
    if (target[i] == '\0' && (dir[i] == '/' || dir[i] == '\0')) {
        cut = i;
    }
    out[0] = '\0';
    for (i = cut; dir[i] != '\0'; i++) {
        if (dir[i] == '/' && dir[i + 1] != '\0') {
            if (n + 3u >= max) {
                return -1;
            }
            memcpy(out + n, "../", 3u);
            n += 3u;
        }
    }
    {
        const char *rest = target + cut;

        while (*rest == '/') {
            rest++;
        }
        if (n + strlen(rest) + 1u > max) {
            return -1;
        }
        memcpy(out + n, rest, strlen(rest) + 1u);
    }
    return (out[0] != '\0') ? 0 : -1;
}

/**
 * @brief Stop on a failure and put "would you like to continue?" up.
 *
 * The item is NOT counted yet: whether it counts as failed or as the last
 * thing the job did depends on the answer.  A delete failure is told apart
 * because the original offers no Continue for one -- there is nothing left
 * to continue to.
 *
 * @return 0, so the caller can `return fail_ask(...)` from a step.
 */
static int
fail_ask(tiku_fs_t *fs, const char *msg, int can_continue)
{
    snprintf(fs->prog.error, sizeof fs->prog.error, "%s", msg);
    fs->prog.error_ask = 1;
    fs->prog.error_can_continue = can_continue;
    return 0;
}

/** @brief Note that the job stopped part-way and drop the rest. */
static int
give_up(tiku_fs_t *fs)
{
    fs->prog.cancelled = 1;
    fs->qcursor = fs->qcount;
    return 1;
}

static int
step_one(tiku_fs_t *fs)
{
    const char *src = fs->queue[fs->qcursor];
    char dstdir[TIKU_PATH_MAX], dstpath[TIKU_PATH_MAX];
    char name[TIKU_NAME_MAX];
    char err[256];

    if (cancelled(fs)) {
        return give_up(fs);
    }
    snprintf(fs->prog.current, sizeof fs->prog.current, "%s", src);
    err[0] = '\0';

    if (fs->prog.op == TIKU_OP_TRASH && tiku_fs_has_trash(fs) &&
        strstr(src, "/" TIKU_TRASH_NAME) == NULL) {
        /* A real Trash move: the item is RENAMED into the Trash, keeping
         * a record of where it came from so it can be put back.  An item
         * already in the Trash falls through to deletion, which is the
         * only thing left to do to it. */
        char trash[TIKU_PATH_MAX], want[TIKU_NAME_MAX];

        if (tiku_fs_trash_dir(fs, src, trash, sizeof trash) != 0) {
            /* This item's filesystem has no writable Trash.  Deleting is
             * what is left, and the caller has already asked -- the
             * question it put to the user was a straight delete precisely
             * because this call would fail (FS-044). */
            trash[0] = '\0';
        }
        if (trash[0] != '\0') {
        /* A name already taken in the Trash gets the same " copy" treatment
         * a duplicate does, so trashing two files of one name keeps both. */
        if (exists(fs, trash, leaf_of(src))) {
            (void)tiku_fs_duplicate_name(fs, trash, leaf_of(src), want,
                                             sizeof want);
        } else {
            snprintf(want, sizeof want, "%s", leaf_of(src));
        }
        if (join(dstpath, sizeof dstpath, trash, want) != 0 ||
            rename(src, dstpath) != 0) {
            snprintf(fs->prog.error, sizeof fs->prog.error,
                     "cannot move '%.80s' to the Trash (%.60s)",
                     leaf_of(src), strerror(errno));
            fs->prog.failed++;
        } else {
            /* Recorded AFTER the move, under the name it now has: this
             * store is keyed by path, so a record written before would be
             * filed under a name nothing answers to. */
            record_origin(fs, dstpath, src);
            /* Undoing a trash move puts the item back where it was, which
             * is the same shape as undoing any other move. */
            undo_push(fs, dstpath, src, 0);
            landed(fs, src, dstpath);
            fs->prog.done++;
        }
        fs->qcursor++;
        return 1;
        }
    }
    if (fs->prog.op == TIKU_OP_TRASH ||
        fs->prog.op == TIKU_OP_DELETE) {
        if (on_device(fs, src)) {
            /*
             * A device node is not a file, and nothing in the backend
             * contract removes anything -- write is the whole of what a
             * store can be asked to change.  So this cannot be done, and
             * the only question is how it fails.
             *
             * It used to call unlink() with the path the NAMESPACE knows
             * -- /devices/board1/gpio/17 -- which no local filesystem has
             * ever heard of.  The kernel said ENOENT, and "no such file"
             * reads as the item being already gone, which is the one
             * thing it is not.
             *
             * Refused by name instead, the way a folder that cannot cross
             * stores is refused: a limit that says what it is can be
             * worked with, and a limit that reports somebody else's errno
             * cannot.  Continuable, so the rest of a queue that spans a
             * board and the disk still goes.
             */
            char msg[240];

            snprintf(msg, sizeof msg,
                     "\"%.60s\" is on a device: the connection can read "
                     "and write its nodes, but not remove them.",
                     leaf_of(src));
            return fail_ask(fs, msg, 1);
        } else if (is_dir(src) && !fs->qexpanded[fs->qcursor]) {
            /* A folder goes children-first: each child is queued as an
             * item of its own, then the folder again, so a child that
             * cannot go is reported by NAME and its siblings still go --
             * the walk continues past individual failures rather than
             * abandoning the tree (FS-049). */
            DIR *d = opendir(src);
            int before = fs->qcount;

            if (d != NULL) {
                struct dirent *e;

                while ((e = readdir(d)) != NULL &&
                       fs->qcount < FS_QUEUE_MAX - 1) {
                    if (strcmp(e->d_name, ".") == 0 ||
                        strcmp(e->d_name, "..") == 0) {
                        continue;
                    }
                    if (join(fs->queue[fs->qcount], TIKU_PATH_MAX,
                             src, e->d_name) == 0) {
                        fs->qexpanded[fs->qcount] = 0;
                        fs->qcount++;
                    }
                }
                (void)closedir(d);
            }
            if (fs->qcount > before && fs->qcount < FS_QUEUE_MAX) {
                /* The folder's second visit, after its children. */
                fs->qexpanded[fs->qcount] = 1;
                snprintf(fs->queue[fs->qcount], TIKU_PATH_MAX, "%s",
                         src);
                fs->qcount++;
                fs->prog.total = fs->qcount;
                fs->qcursor++;
                return 1;
            }
            /* Nothing inside (or no room to expand): fall through to the
             * straight attempt below by treating it as visited. */
            goto delete_attempt;
        } else {
delete_attempt:
            if (remove(src) != 0) {
                if (errno == ENOTEMPTY || errno == EEXIST) {
                    /* Its grandchildren may still be queued behind this
                     * visit: as long as a descendant is pending, the
                     * folder steps to the back of the line instead of
                     * failing -- it can only be empty AFTER them. */
                    size_t plen = strlen(src);
                    int k;

                    for (k = fs->qcursor + 1; k < fs->qcount; k++) {
                        if (strncmp(fs->queue[k], src, plen) == 0 &&
                            fs->queue[k][plen] == '/') {
                            break;
                        }
                    }
                    if (k < fs->qcount && fs->qcount < FS_QUEUE_MAX) {
                        fs->qexpanded[fs->qcount] = 1;
                        snprintf(fs->queue[fs->qcount], TIKU_PATH_MAX,
                                 "%s", src);
                        fs->qcount++;
                        fs->prog.total = fs->qcount;
                        fs->qcursor++;
                        return 1;
                    }
                }
                /* The message is for a human in a dialog: show the leaf
                 * name, which is what they selected, not the whole
                 * path. */
                char msg[240];

                snprintf(msg, sizeof msg,
                         "Error deleting \"%.60s\": %.150s.",
                         leaf_of(src), strerror(errno));
                return fail_ask(fs, msg, 0);
            }
            fs->prog.done++;
        }
        fs->qcursor++;
        return 1;
    }

    if (fs->prog.op == TIKU_OP_LINK) {
        char target[TIKU_PATH_MAX];
        struct stat ss, ds;

        /* "<name> link" ONLY when the plain name is taken, so linking into
         * another folder keeps the original name (FS-009). */
        if (tiku_fs_link_name(fs, fs->dst, leaf_of(src), name,
                                  sizeof name) != 0 ||
            join(dstpath, sizeof dstpath, fs->dst, name) != 0) {
            fs->prog.failed++;
            fs->qcursor++;
            return 1;
        }
        snprintf(target, sizeof target, "%s", src);
        if (fs->link_relative && stat(src, &ss) == 0 &&
            stat(fs->dst, &ds) == 0 && ss.st_dev == ds.st_dev) {
            char rel[TIKU_PATH_MAX];

            /* Across devices a relative path cannot be walked, so the
             * request degrades to an absolute link rather than failing --
             * silently, as the original does (FS-010). */
            if (relative_path(fs->dst, src, rel, sizeof rel) == 0) {
                snprintf(target, sizeof target, "%s", rel);
            }
        }
        if (symlink(target, dstpath) != 0) {
            char msg[240];

            /* A store that cannot hold a link says so in its own terms
             * rather than reporting a generic failure (FS-011). */
            snprintf(msg, sizeof msg, "%s",
                     (errno == EPERM || errno == ENOSYS)
                         ? "The target disk does not support creating links."
                         : "Error creating link.");
            return fail_ask(fs, msg, 1);
        }
        undo_push(fs, dstpath, src, 1);
        fs->prog.done++;
        fs->qcursor++;
        return 1;
    }
    if (fs->prog.op == TIKU_OP_DUPLICATE) {
        /* Each item duplicates into its OWN parent, even when the selection
         * spans folders (FS-008). */
        parent_of(src, dstdir, sizeof dstdir);
        if (tiku_fs_duplicate_name(fs, dstdir, leaf_of(src), name,
                                       sizeof name) != 0) {
            fs->prog.failed++;
            fs->qcursor++;
            return 1;
        }
    } else {
        snprintf(dstdir, sizeof dstdir, "%s", fs->dst);
        snprintf(name, sizeof name, "%s", leaf_of(src));
        {   /* An item already in the destination is silently skipped, not
             * copied onto itself (FS-005). */
            char srcdir[TIKU_PATH_MAX];
            parent_of(src, srcdir, sizeof srcdir);
            if (strcmp(srcdir, dstdir) == 0) {
                fs->prog.skipped++;
                fs->qcursor++;
                return 1;
            }
        }
    }
    if (join(dstpath, sizeof dstpath, dstdir, name) != 0) {
        fs->prog.failed++;
        fs->qcursor++;
        return 1;
    }

    /* Collisions: ask, unless the caller already chose a policy. */
    if (fs->prog.op != TIKU_OP_DUPLICATE && exists(fs, dstdir, name)) {
        switch (fs->policy) {
        case TIKU_CONFLICT_SKIP:
            /* A policy the CALLER set stands for the whole job; answering
             * one question is resolve()'s business, and keeping the
             * distinction in one place is what stops the two disagreeing. */
            fs->prog.skipped++;
            fs->qcursor++;
            return 1;
        case TIKU_CONFLICT_RENAME:
            if (tiku_fs_duplicate_name(fs, dstdir, name, name,
                                           sizeof name) != 0 ||
                join(dstpath, sizeof dstpath, dstdir, name) != 0) {
                fs->prog.failed++;
                fs->qcursor++;
                return 1;
            }
            break;
        case TIKU_CONFLICT_REPLACE:
            if (fs->pending_ask) {
                /* A one-shot "Replace": used up by this item. */
                fs->pending_ask = 0;
                fs->policy = TIKU_CONFLICT_ASK;
            }
            /* Replaced by REMOVING first, not by writing over.  Truncating
             * in place would leave anything holding the old file looking at
             * a half-new one, and a file that cannot be removed is one this
             * job must not start writing into (FS-021).  A directory is
             * left alone: replacing a folder means merging into it. */
            if (!is_dir(dstpath) && remove(dstpath) != 0) {
                char msg[240];

                snprintf(msg, sizeof msg,
                         "There was a problem trying to replace \"%.60s\". "
                         "The item might be open or busy.", name);
                return fail_ask(fs, msg, 1);
            }
            break;
        default:
            fs->prog.conflict = 1;
            snprintf(fs->prog.conflict_path, sizeof fs->prog.conflict_path,
                     "%s", dstpath);
            /* What the question should offer: a FOLDER collision always
             * gets the four-way form, and so does any collision while more
             * than one is pending -- only a lone file-versus-file
             * collision gets the short form (FS-018, FS-019). */
            fs->prog.conflict_isdir = is_dir(dstpath);
            fs->prog.conflict_many = (fs->prog.collisions > 1) ||
                                     fs->prog.conflict_isdir;
            return 0;               /* stop and wait for an answer */
        }
    }

    if (fs->prog.op == TIKU_OP_MOVE) {
        /* Through the tree walker, so a FOLDER moves too -- and moves onto
         * an existing folder of the same name by merging into it rather
         * than refusing or replacing it (FS-029). */
        int rc = move_tree(fs, src, dstpath, err, sizeof err);

        if (rc == -2) {
            return give_up(fs);
        }
        if (rc != 0) {
            char msg[240];

            snprintf(msg, sizeof msg, "Error moving file \"%.60s\": %.150s.",
                     leaf_of(src), err[0] ? err : "move failed");
            return fail_ask(fs, msg, 1);
        }
        undo_push(fs, dstpath, src, 0);
        landed(fs, src, dstpath);
        fs->prog.done++;
    } else {
        /* The same for a copy: a folder already there is merged into, so
         * its children that the source has no counterpart for survive
         * (FS-020). */
        int rc = copy_tree(fs, src, dstpath, err, sizeof err);

        if (rc == -2) {
            /* Cancelled mid-file.  What the copy managed stays, and is
             * recorded, so an undo can still take it away (FS-034). */
            undo_push(fs, dstpath, src, 1);
            return give_up(fs);
        }
        if (rc != 0) {
            char msg[240];

            snprintf(msg, sizeof msg,
                     "Error copying file \"%.60s\": %.150s.", leaf_of(src),
                     err[0] ? err : "copy failed");
            return fail_ask(fs, msg, 1);
        }
        {
            char srcdir[TIKU_PATH_MAX];

            parent_of(src, srcdir, sizeof srcdir);
            if (strcmp(srcdir, dstdir) != 0) {
                copy_pose_info(fs, src, dstpath);
            }
        }
        undo_push(fs, dstpath, src, 1);
        fs->prog.done++;
    }
    fs->qcursor++;
    return 1;
}

int
tiku_fs_step(tiku_fs_t *fs, int budget)
{
    int n = 0;

    if (fs == NULL || fs->prog.conflict || fs->prog.error_ask) {
        return 0;
    }
    while (n < budget && fs->qcursor < fs->qcount) {
        if (!step_one(fs)) {
            break;                  /* stopped on a conflict */
        }
        n++;
    }
    if (fs->qcursor >= fs->qcount && !fs->prog.conflict) {
        fs->prog.op = (fs->prog.total > 0) ? fs->prog.op : TIKU_OP_IDLE;
    }
    return n;
}

void
tiku_fs_resolve(tiku_fs_t *fs, tiku_conflict_t how)
{
    if (fs == NULL || !fs->prog.conflict) {
        return;
    }
    fs->prog.conflict = 0;
    /* The two "all" answers are the only sticky ones: they become the
     * job's standing policy.  The other two answer the collision in hand
     * and leave the next one to ask again, which is the whole difference
     * between "Replace" and "Replace all" (FS-019). */
    switch (how) {
    case TIKU_CONFLICT_SKIP_ALL:
        fs->policy = TIKU_CONFLICT_SKIP;
        break;
    case TIKU_CONFLICT_REPLACE_ALL:
        fs->policy = TIKU_CONFLICT_REPLACE;
        break;
    case TIKU_CONFLICT_SKIP:
        fs->prog.skipped++;
        fs->qcursor++;
        fs->policy = TIKU_CONFLICT_ASK;
        break;
    case TIKU_CONFLICT_REPLACE:
        /* Applied to this item, then back to asking. */
        fs->policy = TIKU_CONFLICT_REPLACE;
        fs->pending_ask = 1;
        break;
    default:
        fs->policy = how;
        break;
    }
}

void
tiku_fs_cancel(tiku_fs_t *fs)
{
    if (fs != NULL) {
        /* What is already done stays done: a half-finished copy is real.
         * The flag rather than a drain, so a copy already inside a file
         * sees it on its next chunk and cleans that file up (FS-033). */
        fs->cancelling = 1;
        fs->qcursor = fs->qcount;
        fs->prog.conflict = 0;
        fs->prog.error_ask = 0;
        fs->prog.cancelled = 1;
    }
}

void
tiku_fs_set_cancel_check(tiku_fs_t *fs, tiku_cancel_fn fn,
                             void *ctx)
{
    if (fs != NULL) {
        fs->cancel_fn = fn;
        fs->cancel_ctx = ctx;
    }
}

void
tiku_fs_resolve_error(tiku_fs_t *fs, int keep_going)
{
    if (fs == NULL || !fs->prog.error_ask) {
        return;
    }
    fs->prog.error_ask = 0;
    fs->prog.failed++;
    fs->qcursor++;
    if (fs->prog.op == TIKU_OP_DELETE) {
        /* FSDeleteFolder keeps iterating past a failure whatever the
         * dialog said -- only a cancel propagates -- so a child that will
         * not go costs that child, not the rest of the tree (FS-049). */
        return;
    }
    if (!keep_going || !fs->prog.error_can_continue) {
        (void)give_up(fs);
    }
}

int
tiku_fs_rename(tiku_fs_t *fs, const char *path, const char *new_name,
                   char *err, size_t errmax)
{
    char dir[TIKU_PATH_MAX], dst[TIKU_PATH_MAX];

    if (fs == NULL || path == NULL || new_name == NULL) {
        return -1;
    }
    if (!tiku_fs_name_ok(new_name, err, errmax)) {
        return -1;
    }
    if (!on_device(fs, path)) {
        tiku_volumes_t vs;
        char why[200];

        if (tiku_volumes_scan(&vs) > 0) {
            /* A volume's name is the FILESYSTEM's label, not the directory
             * entry it happens to be mounted on: renaming the entry would
             * move the mount point and leave the volume called what it was.
             * Refused rather than attempted, because this port has no way
             * to write a label (MA-052). */
            if (tiku_volume_is_root(&vs, path)) {
                if (err != NULL) {
                    snprintf(err, errmax,
                             "\"%.40s\" is a disk: its name is the disk's "
                             "own, which cannot be changed from here.",
                             leaf_of(path));
                }
                return -1;
            }
            /* And the volume has to allow the write at all, said in the
             * words it already has for refusing one. */
            if (!tiku_volume_may_write(&vs, path, 1, why, sizeof why)) {
                if (err != NULL) {
                    snprintf(err, errmax, "%s", why);
                }
                return -1;
            }
        }
    }
    {
        /* The Trash, the Desktop and the root are found BY their names:
         * renaming one strands every rule that looks there (MA-051).
         * Haiku's own refusal is this same predicate list. */
        tiku_model_t m;

        if (fs->backend->ops->stat(fs->backend, path, &m) == 0 &&
            (m.kind == TIKU_KIND_TRASH ||
             m.kind == TIKU_KIND_DESKTOP)) {
            if (err != NULL) {
                snprintf(err, errmax,
                         "The %s cannot be renamed: it is found by its "
                         "name.", (m.kind == TIKU_KIND_TRASH)
                                      ? "Trash" : "Desktop");
            }
            return -1;
        }
    }
    if (strlen(new_name) > 255u) {
        /* NAME_MAX on every host this builds on; said in the source's own
         * words rather than failing the join silently (FS-080). */
        if (err != NULL) {
            snprintf(err, errmax, "The entered name is too long.");
        }
        return -1;
    }
    parent_of(path, dir, sizeof dir);
    if (exists(fs, dir, new_name)) {
        if (err != NULL) {
            snprintf(err, errmax, "'%s' already exists here", new_name);
        }
        return -1;
    }
    if (join(dst, sizeof dst, dir, new_name) != 0) {
        return -1;
    }
    if (rename(path, dst) != 0) {
        if (err != NULL) {
            snprintf(err, errmax, "cannot rename (%s)", strerror(errno));
        }
        return -1;
    }
    undo_reset(fs, TIKU_OP_RENAME);
    undo_push(fs, dst, path, 0);
    landed(fs, path, dst);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* undo                                                                      */
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/* New folder (FS-079)                                                       */
/*---------------------------------------------------------------------------*/

void
tiku_fs_new_folder_name(tiku_fs_t *fs, const char *dir, char *out,
                            size_t max)
{
    long n;

    if (out == NULL || max == 0) {
        return;
    }
    snprintf(out, max, "New folder");
    if (fs == NULL || dir == NULL || !exists(fs, dir, out)) {
        return;
    }
    for (n = 2; n < 10000; n++) {
        /* The missing space past nine is the original's, not a slip: it
         * increments before formatting and switches format string at >9,
         * so "New folder 9" is followed by "New folder10".  Ported as it
         * is, because a folder created by one and found by the other has
         * to be the same folder. */
        if (n <= 9) {
            snprintf(out, max, "New folder %ld", n);
        } else {
            snprintf(out, max, "New folder%ld", n);
        }
        if (!exists(fs, dir, out)) {
            return;
        }
    }
}

int
tiku_fs_new_folder(tiku_fs_t *fs, const char *dir, char *out,
                       size_t max, char *err, size_t errmax)
{
    char name[TIKU_PATH_MAX], path[TIKU_PATH_MAX];

    if (fs == NULL || dir == NULL) {
        return -1;
    }
    tiku_fs_new_folder_name(fs, dir, name, sizeof name);
    if (join(path, sizeof path, dir, name) != 0 || mkdir(path, 0755) != 0) {
        if (err != NULL) {
            snprintf(err, errmax, "Sorry, could not create a new folder.");
        }
        return -1;
    }
    undo_reset(fs, TIKU_OP_NEW_FOLDER);
    undo_push(fs, path, NULL, 1);
    if (out != NULL) {
        snprintf(out, max, "%s", path);
    }
    return 0;
}

int
tiku_fs_new_from_template(tiku_fs_t *fs, const char *template_path,
                              const char *dst_dir, char *out, size_t max,
                              char *err, size_t errmax)
{
    struct stat st;
    char name[TIKU_NAME_MAX];
    char candidate[TIKU_NAME_MAX];
    char dst[TIKU_PATH_MAX];
    const char *base;
    int n;

    if (err != NULL && errmax > 0) {
        err[0] = '\0';
    }
    if (fs == NULL || template_path == NULL || dst_dir == NULL ||
        out == NULL || max == 0) {
        if (err != NULL) {
            snprintf(err, errmax, "template destination is unavailable");
        }
        return -1;
    }
    /* Templates are host filesystem objects.  A device namespace has no
     * local tree to copy and must not fall through to the POSIX walkers. */
    if (fs->backend == NULL || on_device(fs, dst_dir) ||
        lstat(template_path, &st) != 0 ||
        (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) &&
         !S_ISLNK(st.st_mode))) {
        if (err != NULL) {
            snprintf(err, errmax, "could not read template");
        }
        return -1;
    }
    base = leaf_of(template_path);
    if (base[0] == '\0' || strlen(base) >= sizeof name - 5u) {
        if (err != NULL) {
            snprintf(err, errmax, "template name is too long");
        }
        return -1;
    }
    snprintf(name, sizeof name, "New %s", base);
    snprintf(candidate, sizeof candidate, "%s", name);
    if (exists(fs, dst_dir, candidate)) {
        for (n = 2; n < 10000; n++) {
            snprintf(candidate, sizeof candidate, "New %s %d", base, n);
            if (!exists(fs, dst_dir, candidate)) {
                break;
            }
        }
        if (n >= 10000) {
            if (err != NULL) {
                snprintf(err, errmax, "could not find a free template name");
            }
            return -1;
        }
    }
    if (join(dst, sizeof dst, dst_dir, candidate) != 0) {
        if (err != NULL) {
            snprintf(err, errmax, "template destination path is too long");
        }
        return -1;
    }
    if (copy_tree(fs, template_path, dst, err, errmax) != 0) {
        return -1;
    }
    copy_pose_info(fs, template_path, dst);
    undo_reset(fs, TIKU_OP_COPY);
    undo_push(fs, dst, NULL, 1);
    snprintf(out, max, "%s", dst);
    return 0;
}

int
tiku_fs_can_undo(const tiku_fs_t *fs, char *what, size_t max)
{
    const undo_op_t *o;

    if (fs == NULL || fs->ucount == 0) {
        return 0;
    }
    o = &fs->ustack[fs->ucount - 1];
    if (o->n == 0) {
        return 0;
    }
    /* A delete is gone; saying "Undo Delete" and failing would be worse
     * than not offering it. */
    if (o->op == TIKU_OP_DELETE) {
        return 0;
    }
    if (what != NULL) {
        snprintf(what, max, "%s", tiku_fs_op_name(o->op));
    }
    return 1;
}

int
tiku_fs_last_created(const tiku_fs_t *fs,
                         char out[][TIKU_PATH_MAX], int max)
{
    const undo_op_t *o;
    int i, n = 0;

    if (fs == NULL || out == NULL || max <= 0 || fs->ucount <= 0) {
        return 0;
    }
    o = &fs->ustack[fs->ucount - 1];
    for (i = 0; i < o->n && n < max; i++) {
        /* An entry the operation brought into being: created outright, or
         * the destination half of a copy (its 'to' names the origin). */
        if (o->rec[i].created || o->rec[i].to[0] != '\0') {
            snprintf(out[n], TIKU_PATH_MAX, "%s", o->rec[i].from);
            n++;
        }
    }
    return n;
}

int
tiku_fs_can_redo(const tiku_fs_t *fs, char *what, size_t max)
{
    const undo_op_t *o;

    if (fs == NULL || fs->rcount == 0) {
        return 0;
    }
    o = &fs->rstack[fs->rcount - 1];
    if (o->n == 0) {
        return 0;
    }
    if (what != NULL) {
        snprintf(what, max, "%s", tiku_fs_op_name(o->op));
    }
    return 1;
}

const char *
tiku_fs_op_name(tiku_op_t op)
{
    static const char *names[] = {
        "", "Copy", "Move", "Duplicate", "Move to Trash", "Delete",
        "Rename", "Create Link", "New Folder"
    };

    if ((size_t)op >= sizeof names / sizeof names[0]) {
        return "";
    }
    return names[op];
}

/**
 * @brief Put one record back the way it was.
 *
 * @return 1 when something was undone.
 */
static int
undo_one(tiku_fs_t *fs, const undo_rec_t *r, char *err, size_t errmax)
{
    if (r->created) {
        /* Whatever the operation made goes, tree and all: a copied FOLDER
         * has children the copy created too, and remove() would leave the
         * lot behind on the grounds that the directory is not empty. */
        return (remove_tree(r->from) == 0);
    }
    if (rename(r->from, r->to) == 0) {
        landed(fs, r->from, r->to);
        return 1;
    }
    if (err != NULL) {
        snprintf(err, errmax, "cannot undo '%s' (%s)", r->from,
                 strerror(errno));
    }
    return 0;
}

/**
 * @brief Do one record over again, in the direction the operation went.
 *
 * The record does not say what the operation WAS, so the kind is passed in:
 * putting a copy back means copying again, while putting a move back means
 * moving, and the two records look identical.
 */
static int
redo_one(tiku_fs_t *fs, tiku_op_t op, const undo_rec_t *r, char *err,
         size_t errmax)
{
    if (op == TIKU_OP_NEW_FOLDER) {
        return (make_path(r->from) == 0);
    }
    if (r->created) {
        if (r->to[0] == '\0') {
            return 0;               /* nothing recorded to repeat from   */
        }
        return (copy_tree(fs, r->to, r->from, err, errmax) == 0);
    }
    if (rename(r->to, r->from) == 0) {
        landed(fs, r->to, r->from);
        return 1;
    }
    if (err != NULL) {
        snprintf(err, errmax, "cannot redo '%s' (%s)", r->to,
                 strerror(errno));
    }
    return 0;
}

int
tiku_fs_undo(tiku_fs_t *fs, char *err, size_t errmax)
{
    undo_op_t *o;
    int i, n = 0;

    if (!tiku_fs_can_undo(fs, NULL, 0)) {
        return -1;
    }
    o = &fs->ustack[fs->ucount - 1];
    /* Reverse order, so a sequence that renamed around a collision unwinds
     * without colliding with itself. */
    for (i = o->n - 1; i >= 0; i--) {
        n += undo_one(fs, &o->rec[i], err, errmax);
    }
    /* Moved to the redo stack whatever the individual records did: the
     * original ignores Undo()'s return code too, and a partially reversed
     * operation is still the thing the user would want back. */
    (void)undo_stack_push(fs->rstack, &fs->rcount, o);
    memset(o, 0, sizeof *o);
    fs->ucount--;
    return n;
}

int
tiku_fs_redo(tiku_fs_t *fs, char *err, size_t errmax)
{
    undo_op_t *o;
    int i, n = 0;

    if (!tiku_fs_can_redo(fs, NULL, 0)) {
        return -1;
    }
    o = &fs->rstack[fs->rcount - 1];
    for (i = 0; i < o->n; i++) {
        n += redo_one(fs, o->op, &o->rec[i], err, errmax);
    }
    (void)undo_stack_push(fs->ustack, &fs->ucount, o);
    memset(o, 0, sizeof *o);
    fs->rcount--;
    return n;
}

/** @brief tiku_backend_text's listing half: append one child's name. */
typedef struct {
    char  *out;
    size_t max, used;
} backend_text_ctx_t;

static int
backend_text_entry(const tiku_model_t *m, void *ctx)
{
    backend_text_ctx_t *t = ctx;

    t->used += (size_t)snprintf(t->out + t->used,
                                (t->used < t->max) ? t->max - t->used : 0u,
                                "%s|", m->name);
    return (t->used < t->max) ? 0 : 1;      /* full: stop the walk */
}

int
tiku_backend_text(tiku_backend_t *b, const char *path,
                  char *out, size_t max)
{
    tiku_model_t m;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (b == NULL || b->ops == NULL || path == NULL || path[0] != '/' ||
        b->ops->stat(b, path, &m) != 0) {
        return 0;               /* nothing serves it: told, not guessed */
    }
    if (tiku_model_is_container(&m)) {
        backend_text_ctx_t t = { out, max, 0u };

        if (b->ops->list != NULL) {
            (void)b->ops->list(b, path, backend_text_entry, &t);
        }
        return 1;
    }
    if (b->ops->read != NULL) {
        int n = b->ops->read(b, path, out, max - 1u);

        if (n < 0) {
            n = 0;
        }
        if ((size_t)n >= max) {
            n = (int)max - 1;
        }
        out[n] = '\0';
    }
    return 1;
}
