/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mount.c - one namespace over several backends.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_mount.h"

typedef struct {
    char             prefix[TIKU_PATH_MAX];
    tiku_backend_t  *backend;
    int              owned;
    int              live;
} mount_slot_t;

typedef struct {
    mount_slot_t slot[TIKU_MOUNT_MAX];
} mount_impl_t;

/*
 * The mount an id came from, in its top byte.  Fifty-six bits are left
 * for the backend's own identity, which is every inode any filesystem
 * here will ever issue; a store that used the top byte would alias two
 * of its nodes onto one, and that is written down rather than guarded
 * because the guard would cost a branch on every entry of every listing.
 */
#define ID_SHIFT 56
#define ID_MASK  0x00FFFFFFFFFFFFFFull

static uint64_t
tag_id(int slot, uint64_t raw)
{
    return ((uint64_t)(slot + 1) << ID_SHIFT) | (raw & ID_MASK);
}

static int
id_slot(uint64_t id)
{
    int s = (int)((id >> ID_SHIFT) & 0xFFu) - 1;

    return (s >= 0 && s < TIKU_MOUNT_MAX) ? s : -1;
}

static mount_impl_t *
impl_of(const tiku_backend_t *b)
{
    return (b != NULL) ? (mount_impl_t *)b->impl : NULL;
}

/** @brief Whether @p prefix names @p path or an ancestor of it. */
static int
covers(const char *prefix, const char *path)
{
    size_t n;

    if (prefix[0] == '/' && prefix[1] == '\0') {
        return 1;               /* the root claims everything */
    }
    n = strlen(prefix);
    if (strncmp(path, prefix, n) != 0) {
        return 0;
    }
    /* A prefix must end on a component boundary: /devices/board1 does
     * not claim /devices/board10. */
    return path[n] == '\0' || path[n] == '/';
}

/** @brief @p path as the backend at @p prefix knows it. */
static void
strip(const char *prefix, const char *path, char *out, size_t max)
{
    const char *rest;

    if (prefix[0] == '/' && prefix[1] == '\0') {
        snprintf(out, max, "%s", path);
        return;
    }
    rest = path + strlen(prefix);
    if (rest[0] == '\0') {
        snprintf(out, max, "/");
    } else {
        snprintf(out, max, "%s", rest);
    }
}

/** @brief The mounted name for @p local, which the backend at @p i gave. */
static void
graft(const char *prefix, const char *local, char *out, size_t max)
{
    if (prefix[0] == '/' && prefix[1] == '\0') {
        snprintf(out, max, "%s", local);
        return;
    }
    if (local[0] == '/' && local[1] == '\0') {
        snprintf(out, max, "%s", prefix);
    } else {
        snprintf(out, max, "%s%s", prefix, local);
    }
}

/** @brief The slot serving @p path: the longest prefix that claims it. */
static int
route_slot(const mount_impl_t *m, const char *path)
{
    int i, best = -1;
    size_t bestlen = 0;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        size_t n;

        if (!m->slot[i].live || !covers(m->slot[i].prefix, path)) {
            continue;
        }
        n = strlen(m->slot[i].prefix);
        if (best < 0 || n > bestlen) {
            best = i;
            bestlen = n;
        }
    }
    return best;
}

/**
 * @brief The component of a mount prefix that would sit directly in @p dir.
 *
 * /devices/board1 seen from /        gives "devices"
 * /devices/board1 seen from /devices gives "board1"
 * and anything not under @p dir gives nothing.
 *
 * @return 1 when @p out was written.
 */
static int
child_toward(const char *prefix, const char *dir, char *out, size_t max)
{
    const char *rest;
    size_t n;
    const char *slash;

    if (prefix[0] == '/' && prefix[1] == '\0') {
        return 0;               /* the root is nobody's child */
    }
    if (dir[0] == '/' && dir[1] == '\0') {
        rest = prefix + 1;
    } else {
        n = strlen(dir);
        if (strncmp(prefix, dir, n) != 0 || prefix[n] != '/') {
            return 0;
        }
        rest = prefix + n + 1;
    }
    if (rest[0] == '\0') {
        return 0;
    }
    slash = strchr(rest, '/');
    if (slash != NULL) {
        size_t len = (size_t)(slash - rest);

        if (len + 1u > max) {
            return 0;
        }
        memcpy(out, rest, len);
        out[len] = '\0';
    } else {
        snprintf(out, max, "%s", rest);
    }
    return out[0] != '\0';
}

/** @brief Whether @p path is an ancestor of some mount, or a mount root. */
static int
is_synthetic_dir(const mount_impl_t *m, const char *path)
{
    char kid[TIKU_NAME_MAX];
    int i;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (m->slot[i].live &&
            child_toward(m->slot[i].prefix, path, kid, sizeof kid)) {
            return 1;
        }
    }
    return 0;
}

/** @brief A directory that exists because something is mounted under it. */
static void
synth_model(const char *path, tiku_model_t *out)
{
    const char *leaf = strrchr(path, '/');

    memset(out, 0, sizeof *out);
    snprintf(out->path, sizeof out->path, "%s", path);
    snprintf(out->name, sizeof out->name, "%s",
             (leaf != NULL && leaf[1] != '\0') ? leaf + 1 : path);
    snprintf(out->type, sizeof out->type, "%s", "inode/directory");
    out->kind = TIKU_KIND_DIRECTORY;
    out->facts.perm = TIKU_P_READ;
    out->facts.cap_known = 1;
    snprintf(out->facts.req_cap, sizeof out->facts.req_cap, "%s", "-");
    snprintf(out->facts.meta, sizeof out->facts.meta, "%s", "-");
    /*
     * Slot zero of the id space, which no mount uses: a synthetic
     * directory has an identity of its own so a view can tell two of
     * them apart, and it must not collide with a real node's.
     */
    {
        uint64_t h = 1469598103934665603ull;
        const char *p;

        for (p = path; *p != '\0'; p++) {
            h = (h ^ (uint64_t)(unsigned char)*p) * 1099511628211ull;
        }
        out->node_id = h & ID_MASK;
    }
}

/*---------------------------------------------------------------------------*/
/* The ops                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Bring one model back into the mounted namespace. */
static void
adopt(tiku_backend_t *router, const mount_impl_t *m, int slot,
      tiku_model_t *out)
{
    char mounted[TIKU_PATH_MAX];

    graft(m->slot[slot].prefix, out->path, mounted, sizeof mounted);
    snprintf(out->path, sizeof out->path, "%s", mounted);
    /*
     * The mount POINT is named by the mount, not by what is mounted.
     * A device asked about its own root answers with whatever it calls
     * that root -- "/", or nothing at all -- and a row in /devices
     * showing an empty name is the device that cannot be told from the
     * next one.  Inside the mount the backend's own names stand.
     */
    if (strcmp(mounted, m->slot[slot].prefix) == 0) {
        const char *leaf = strrchr(m->slot[slot].prefix, '/');

        if (leaf != NULL && leaf[1] != '\0') {
            snprintf(out->name, sizeof out->name, "%s", leaf + 1);
        }
    }
    out->node_id = tag_id(slot, out->node_id);
    /*
     * Every model leaves here owned by the ROUTER, whatever answered.
     * A caller keeps the model and later asks its backend to read its
     * path -- and that path is now a mounted one, which only the router
     * can resolve.  Handing back the inner backend would send a mounted
     * path to something that has never heard of the prefix.
     */
    out->backend = router;
}

static int
mount_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    mount_impl_t *m = impl_of(b);
    char local[TIKU_PATH_MAX];
    int slot;

    if (m == NULL || path == NULL || out == NULL) {
        return -1;
    }
    slot = route_slot(m, path);
    if (slot >= 0) {
        tiku_backend_t *inner = m->slot[slot].backend;

        strip(m->slot[slot].prefix, path, local, sizeof local);
        if (inner->ops->stat(inner, local, out) == 0) {
            adopt(b, m, slot, out);
            return 0;
        }
    }
    /* Nobody serves it -- but something may be mounted under it, and
     * then it is a directory whose whole content is those mounts. */
    if (is_synthetic_dir(m, path)) {
        synth_model(path, out);
        out->backend = b;
        return 0;
    }
    return -1;
}

struct list_relay {
    tiku_backend_t *router;
    mount_impl_t   *m;
    int             slot;
    tiku_entry_fn   fn;
    void           *ctx;
};

static int
relay_entry(const tiku_model_t *entry, void *ctx)
{
    struct list_relay *r = ctx;
    tiku_model_t copy = *entry;

    adopt(r->router, r->m, r->slot, &copy);
    return r->fn(&copy, r->ctx);
}

static int
mount_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn, void *ctx)
{
    mount_impl_t *m = impl_of(b);
    char local[TIKU_PATH_MAX];
    int slot, n = 0, i, served = 0, syn = 0;

    if (m == NULL || path == NULL || fn == NULL) {
        return -1;
    }
    slot = route_slot(m, path);
    if (slot >= 0) {
        tiku_backend_t *inner = m->slot[slot].backend;
        struct list_relay relay;
        int got;

        strip(m->slot[slot].prefix, path, local, sizeof local);
        relay.router = b;
        relay.m = m;
        relay.slot = slot;
        relay.fn = fn;
        relay.ctx = ctx;
        got = inner->ops->list(inner, local, relay_entry, &relay);
        if (got >= 0) {
            n = got;
            served = 1;
        }
    }
    /*
     * And then whatever is mounted beneath, whether or not anything
     * served the directory itself: /devices is nobody's directory but
     * it has children, and / is the local root AND has /devices in it.
     */
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        char kid[TIKU_NAME_MAX], full[TIKU_PATH_MAX];
        tiku_model_t entry;
        int j, dup = 0;

        if (!m->slot[i].live ||
            !child_toward(m->slot[i].prefix, path, kid, sizeof kid)) {
            continue;
        }
        /* Two devices under /devices both put "devices" in the root:
         * the row is the shared component, offered once. */
        for (j = 0; j < i; j++) {
            char other[TIKU_NAME_MAX];

            if (m->slot[j].live &&
                child_toward(m->slot[j].prefix, path, other,
                             sizeof other) &&
                strcmp(other, kid) == 0) {
                dup = 1;
            }
        }
        if (dup) {
            continue;
        }
        if (path[1] == '\0') {
            snprintf(full, sizeof full, "/%s", kid);
        } else {
            snprintf(full, sizeof full, "%s/%s", path, kid);
        }
        /*
         * The mount itself, asked of the backend that IS it, so a device
         * root shows as the device rather than as a bare folder; only a
         * shared ancestor is synthesised.
         */
        if (mount_stat(b, full, &entry) != 0) {
            continue;
        }
        n++;
        syn++;
        if (fn(&entry, ctx) != 0) {
            return n;
        }
    }
    /*
     * A directory nobody served and nothing is mounted under is NOT an
     * empty directory: the caller asked about something that is not
     * there, and answering "no entries" would have a folder window open
     * on a path that does not exist rather than say so.
     */
    return (served || syn > 0) ? n : -1;
}

static int
mount_read(tiku_backend_t *b, const char *path, void *buf, size_t max)
{
    mount_impl_t *m = impl_of(b);
    char local[TIKU_PATH_MAX];
    int slot = (m != NULL) ? route_slot(m, path) : -1;

    if (slot < 0) {
        return -1;
    }
    strip(m->slot[slot].prefix, path, local, sizeof local);
    return m->slot[slot].backend->ops->read(m->slot[slot].backend, local,
                                            buf, max);
}

static int
mount_write(tiku_backend_t *b, const char *path, const void *buf, size_t len,
            char *err, size_t errmax)
{
    mount_impl_t *m = impl_of(b);
    char local[TIKU_PATH_MAX];
    int slot = (m != NULL) ? route_slot(m, path) : -1;

    if (slot < 0) {
        snprintf(err, errmax, "nothing is mounted at '%.60s'", path);
        return -1;
    }
    strip(m->slot[slot].prefix, path, local, sizeof local);
    return m->slot[slot].backend->ops->write(m->slot[slot].backend, local,
                                             buf, len, err, errmax);
}

static int
mount_pump(tiku_backend_t *b, int timeout_ms)
{
    mount_impl_t *m = impl_of(b);
    int i, n = 0;

    if (m == NULL) {
        return -1;
    }
    /*
     * Every mount, each with the whole budget.  That is wrong the moment
     * a device stops answering -- one dead link would spend the timeout
     * on every frame while the others wait behind it -- and it is what
     * the sessions being blocking forces today.  When they poll, this
     * becomes one pass with a shared budget and the comment goes.
     */
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        int got;

        if (!m->slot[i].live || m->slot[i].backend->ops->pump == NULL) {
            continue;
        }
        got = m->slot[i].backend->ops->pump(m->slot[i].backend, timeout_ms);
        if (got > 0) {
            n += got;
        }
    }
    return n;
}

static int
mount_changes(tiku_backend_t *b, tiku_model_change_t *out, int max,
              int *complete)
{
    mount_impl_t *m = impl_of(b);
    int i, n = 0, any = 0;

    if (m == NULL || out == NULL || max <= 0) {
        return -1;
    }
    if (complete != NULL) {
        *complete = 1;
    }
    for (i = 0; i < TIKU_MOUNT_MAX && n < max; i++) {
        int got, k, mine = 1;

        if (!m->slot[i].live || m->slot[i].backend->ops->changes == NULL) {
            continue;
        }
        got = m->slot[i].backend->ops->changes(m->slot[i].backend, out + n,
                                               max - n, &mine);
        if (got < 0) {
            /*
             * A backend that cannot tell means POLL -- and it means it
             * for the whole namespace, because a caller cannot poll one
             * mount and trust notices from another.  Saying so is the
             * honest answer; claiming the mounts that DO know would have
             * a view believing nothing changed on the one that does not.
             */
            return -1;
        }
        any = 1;
        for (k = 0; k < got; k++) {
            out[n + k].id = tag_id(i, out[n + k].id);
        }
        n += got;
        if (!mine && complete != NULL) {
            *complete = 0;
        }
    }
    return any ? n : -1;
}

static int
mount_path_of_id(tiku_backend_t *b, uint64_t id, char *out, size_t max)
{
    mount_impl_t *m = impl_of(b);
    int slot = id_slot(id);
    char local[TIKU_PATH_MAX];

    if (m == NULL || slot < 0 || !m->slot[slot].live ||
        m->slot[slot].backend->ops->path_of_id == NULL) {
        return -1;
    }
    if (m->slot[slot].backend->ops->path_of_id(m->slot[slot].backend,
                                               id & ID_MASK, local,
                                               sizeof local) != 0) {
        return -1;
    }
    graft(m->slot[slot].prefix, local, out, max);
    return 0;
}

static int
mount_setperm(tiku_backend_t *b, const char *path, unsigned perm,
              char *err, size_t errmax)
{
    mount_impl_t *m = impl_of(b);
    char local[TIKU_PATH_MAX];
    int slot = (m != NULL) ? route_slot(m, path) : -1;

    if (slot < 0 || m->slot[slot].backend->ops->setperm == NULL) {
        snprintf(err, errmax, "'%.60s' does not take permissions", path);
        return -1;
    }
    strip(m->slot[slot].prefix, path, local, sizeof local);
    return m->slot[slot].backend->ops->setperm(m->slot[slot].backend, local,
                                               perm, err, errmax);
}

static struct tiku_store *
mount_state_store(tiku_backend_t *b)
{
    mount_impl_t *m = impl_of(b);
    int slot;

    if (m == NULL) {
        return NULL;
    }
    /*
     * The ROOT's store.  The call takes no path, so it cannot be routed,
     * and the one thing above this that asks for it wants where a pose
     * position is kept -- which is the local side.  A per-mount store
     * needs the question to name a node first.
     */
    slot = route_slot(m, "/");
    if (slot < 0 || m->slot[slot].backend->ops->state_store == NULL) {
        return NULL;
    }
    return m->slot[slot].backend->ops->state_store(m->slot[slot].backend);
}

static void
mount_close(tiku_backend_t *b)
{
    mount_impl_t *m = impl_of(b);
    int i;

    if (m != NULL) {
        for (i = 0; i < TIKU_MOUNT_MAX; i++) {
            if (m->slot[i].live && m->slot[i].owned &&
                m->slot[i].backend->ops->close != NULL) {
                m->slot[i].backend->ops->close(m->slot[i].backend);
            }
        }
        free(m);
    }
    free(b);
}

static const tiku_backend_ops_t mount_ops = {
    "mount",
    mount_stat,
    mount_list,
    mount_read,
    mount_write,
    mount_pump,
    mount_changes,
    mount_path_of_id,
    mount_setperm,
    mount_state_store,
    mount_close
};

/*---------------------------------------------------------------------------*/
/* The table                                                                 */
/*---------------------------------------------------------------------------*/

tiku_backend_t *
tiku_mount_open(void)
{
    tiku_backend_t *b = calloc(1u, sizeof *b);
    mount_impl_t *m = calloc(1u, sizeof *m);

    if (b == NULL || m == NULL) {
        free(b);
        free(m);
        return NULL;
    }
    b->ops = &mount_ops;
    b->impl = m;
    b->devid[0] = '\0';
    return b;
}

/** @brief Whether @p prefix is a shape a mount may take. */
static int
prefix_ok(const char *prefix)
{
    size_t n;

    if (prefix == NULL || prefix[0] != '/') {
        return 0;
    }
    n = strlen(prefix);
    if (n + 1u >= TIKU_PATH_MAX) {
        return 0;
    }
    if (n > 1u && prefix[n - 1u] == '/') {
        return 0;               /* no trailing slash but on the root */
    }
    return 1;
}

int
tiku_mount_add(tiku_backend_t *router, const char *prefix,
               tiku_backend_t *b, int owned)
{
    mount_impl_t *m = impl_of(router);
    int i, spare = -1;

    if (m == NULL || b == NULL || !prefix_ok(prefix)) {
        return -1;
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (m->slot[i].live) {
            if (strcmp(m->slot[i].prefix, prefix) == 0) {
                return -1;      /* taken: unmount before mounting over */
            }
        } else if (spare < 0) {
            spare = i;
        }
    }
    if (spare < 0) {
        return -1;
    }
    snprintf(m->slot[spare].prefix, sizeof m->slot[spare].prefix, "%s",
             prefix);
    /*
     * The namespace answers for its ROOT's kind of storage.  tiku_fs
     * asks the backend whether it is a device to decide whether a copy
     * goes through the backend or through the local filesystem, and a
     * namespace that said "not a device" over a device root would send
     * every copy down the POSIX path.  Only the root: a device mounted
     * under /devices does not make the local files into a device, and
     * that is exactly the case a per-PATH answer has to serve -- which
     * this one still cannot, and is why cross-mount copy is its own
     * piece of work rather than something that falls out of here.
     */
    if (prefix[0] == '/' && prefix[1] == '\0') {
        snprintf(router->devid, sizeof router->devid, "%s", b->devid);
    }
    m->slot[spare].backend = b;
    m->slot[spare].owned = owned ? 1 : 0;
    m->slot[spare].live = 1;
    return 0;
}

int
tiku_mount_remove(tiku_backend_t *router, const char *prefix)
{
    mount_impl_t *m = impl_of(router);
    int i;

    if (m == NULL || prefix == NULL) {
        return -1;
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (m->slot[i].live && strcmp(m->slot[i].prefix, prefix) == 0) {
            if (m->slot[i].owned &&
                m->slot[i].backend->ops->close != NULL) {
                m->slot[i].backend->ops->close(m->slot[i].backend);
            }
            /*
             * Emptied where it stands.  Sliding the table down would
             * renumber the mounts, and the number is in the top byte of
             * every id already handed out -- a change notice arriving
             * after a device left would then name a node on a different
             * device.
             */
            m->slot[i].live = 0;
            m->slot[i].backend = NULL;
            m->slot[i].prefix[0] = '\0';
            return 0;
        }
    }
    return -1;
}

int
tiku_mount_count(const tiku_backend_t *router)
{
    const mount_impl_t *m = impl_of(router);
    int i, n = 0;

    if (m == NULL) {
        return 0;
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (m->slot[i].live) {
            n++;
        }
    }
    return n;
}

int
tiku_mount_slots(const tiku_backend_t *router)
{
    return (impl_of(router) != NULL) ? TIKU_MOUNT_MAX : 0;
}

const char *
tiku_mount_prefix_at(const tiku_backend_t *router, int i)
{
    const mount_impl_t *m = impl_of(router);

    if (m == NULL || i < 0 || i >= TIKU_MOUNT_MAX || !m->slot[i].live) {
        return NULL;
    }
    return m->slot[i].prefix;
}

tiku_backend_t *
tiku_mount_at(const tiku_backend_t *router, int i)
{
    const mount_impl_t *m = impl_of(router);

    if (m == NULL || i < 0 || i >= TIKU_MOUNT_MAX || !m->slot[i].live) {
        return NULL;
    }
    return m->slot[i].backend;
}

int
tiku_mount_is(const tiku_backend_t *b)
{
    return b != NULL && b->ops == &mount_ops;
}

tiku_backend_t *
tiku_backend_serving(tiku_backend_t *b, const char *path, char *local,
                     size_t max)
{
    if (b == NULL) {
        return NULL;
    }
    if (!tiku_mount_is(b)) {
        if (local != NULL) {
            snprintf(local, max, "%s", (path != NULL) ? path : "");
        }
        return b;
    }
    return tiku_mount_route(b, path, local, max);
}

tiku_backend_t *
tiku_mount_route(const tiku_backend_t *router, const char *path, char *local,
                 size_t max)
{
    const mount_impl_t *m = impl_of(router);
    int slot;

    if (m == NULL || path == NULL) {
        return NULL;
    }
    slot = route_slot(m, path);
    if (slot < 0) {
        return NULL;
    }
    if (local != NULL) {
        strip(m->slot[slot].prefix, path, local, max);
    }
    return m->slot[slot].backend;
}
