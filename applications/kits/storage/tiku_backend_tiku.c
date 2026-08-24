/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_backend_tiku.c - a live device as a browsable volume.
 *
 * Rides the session/namespace client already proven against three boards
 * (applications/desktop): the manifest supplies structure, typed descriptors
 * and capabilities in one read, `cat`/`write` supply values, and `ls` supplies
 * the runtime contents of a dynamic store like /data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_model.h"
#include "tiku_ident.h"
#include "tiku_state.h"

#include "tiku_ns.h"
#include "tiku_session.h"
#include "tiku_tx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned            last_dropped;   /* device drop count when last asked */
    tiku_session_t *sess;
    tiku_ns_t      *ns;
    tiku_store_t    *store;
    tiku_identity_t  ident;
    char                 devid[TIKU_DEVID_MAX];
} tiku_impl_t;

/** @brief Read one identity node, empty string when absent. */
static void
read_ident_field(tiku_ns_t *ns, const char *path, char *out, size_t max)
{
    const tiku_node_t *n;

    out[0] = '\0';
    if (tiku_ns_read(ns, path) != 0) {
        return;
    }
    n = tiku_ns_find(ns, path);
    if (n != NULL && n->value_valid) {
        size_t i = 0;
        while (n->value[i] != '\0' && n->value[i] != '\n' && i + 1u < max) {
            out[i] = n->value[i];
            i++;
        }
        out[i] = '\0';
    }
}

/**
 * @brief Decide a namespace node's kind.
 *
 * A device directory is VIRTUAL_DIR unless it is a dynamic store, which is a
 * real DIRECTORY; the distinction is what makes /sys read-only in the UI while
 * /data accepts new files.  The manifest does not yet mark dynamic
 * directories, so /data is recognised by path until the firmware emits a
 * distinct type character.
 */
static int
in_store(const char *path)
{
    return (strcmp(path, "/data") == 0 || strncmp(path, "/data/", 6) == 0);
}

static tiku_kind_t
kind_of(const tiku_node_t *n)
{
    if (n->path[0] == '/' && n->path[1] == '\0') {
        return TIKU_KIND_VOLUME;
    }
    if (in_store(n->path)) {
        /* A dynamic store holds FILES -- bytes with a name -- not live
         * values.  Getting this wrong makes every saved BASIC program look
         * like a sensor reading, and denies them file semantics. */
        return n->is_dir ? TIKU_KIND_DIRECTORY : TIKU_KIND_FILE;
    }
    return n->is_dir ? TIKU_KIND_VIRTUAL_DIR : TIKU_KIND_DEVICE_NODE;
}

/** @brief Copy a namespace node into a Model. */
static void
from_node(tiku_backend_t *b, const tiku_node_t *n,
          tiku_model_t *m)
{
    uint64_t h = 1469598103934665603ULL;
    const char *p;

    memset(m, 0, sizeof *m);
    /* Copied with an explicit bound: the namespace fields are larger than the
     * Model's, and a truncated path names a different node. */
    {
        size_t pl = strlen(n->path), nl = strlen(n->name);
        if (pl >= sizeof m->path) { pl = sizeof m->path - 1u; }
        if (nl >= sizeof m->name) { nl = sizeof m->name - 1u; }
        memcpy(m->path, n->path, pl); m->path[pl] = '\0';
        memcpy(m->name, n->name, nl); m->name[nl] = '\0';
    }
    m->kind = kind_of(n);
    m->backend = b;
    m->facts.perm = 0u;
    if (n->perm & TIKU_NS_P_READ)  { m->facts.perm |= TIKU_P_READ; }
    if (n->perm & TIKU_NS_P_WRITE) { m->facts.perm |= TIKU_P_WRITE; }
    /* On a container, WRITE means "entries can be created here", which is
     * what access(W_OK) answers for a local directory.  The manifest's perm
     * column instead describes the NODE's own read/write handlers, and a
     * directory has none -- so every device directory arrives "--".  A
     * dynamic store really is mutable, so say so; a namespace directory
     * really is not, so leave it clear.  (Firmware follow-up: emit a
     * distinct type character for dynamic dirs so this stops being keyed
     * on the path.) */
    if (m->kind == TIKU_KIND_DIRECTORY) {
        m->facts.perm |= TIKU_P_READ | TIKU_P_WRITE;
    } else if (m->kind == TIKU_KIND_VIRTUAL_DIR ||
               m->kind == TIKU_KIND_VOLUME) {
        m->facts.perm |= TIKU_P_READ;
    }
    snprintf(m->facts.req_cap, sizeof m->facts.req_cap, "%s", n->cap);
    snprintf(m->facts.meta, sizeof m->facts.meta, "%s", n->meta);
    /* "?" is what the ls-walk fills in when the manifest was cut, so the UI
     * can tell "open" from "we never learned". */
    m->facts.cap_known = (strcmp(n->cap, "?") != 0);
    m->facts.size = (int64_t)strlen(n->value);
    snprintf(m->type, sizeof m->type, "%s",
             (m->kind == TIKU_KIND_FILE)      ? "tiku-file/data"
             : (m->kind == TIKU_KIND_DIRECTORY) ? "tiku-dir/store"
             : (m->kind == TIKU_KIND_VOLUME)    ? "tiku-dir/device"
             : n->is_dir ? "tiku-dir/namespace" : "tiku-node/value");

    if (n->id != 0u) {
        /* The device's own identity, stable across a rename -- which is what
         * lets a renamed node be recognised as the SAME node instead of one
         * vanishing and another appearing. */
        m->node_id = n->id;
    } else {
        /* An older device has no identity column.  Hashing the path is the
         * only fallback, and it is why a rename there looks like a different
         * node: the identity moves with the name. */
        for (p = n->path; *p != '\0'; p++) {
            h ^= (unsigned char)*p;
            h *= 1099511628211ULL;
        }
        m->node_id = h;
    }
    m->generation = n->generation;
}

/**
 * @brief What the device has recorded since we last asked.
 *
 * Reading the device's change ring is one round trip whatever the size of
 * the namespace, which is what makes it worth asking every tick where
 * re-listing every open directory is not.
 */
static int
tk_changes(tiku_backend_t *b, tiku_model_change_t *out, int max,
           int *complete)
{
    tiku_impl_t *impl = b->impl;
    tiku_ns_change_t recs[32];
    unsigned dropped = 0u;
    int n, i, w = 0;

    if (complete != NULL) {
        *complete = 1;
    }
    n = tiku_ns_events(impl->ns, recs,
                            (int)(sizeof recs / sizeof recs[0]), &dropped);
    if (n < 0) {
        return -1;              /* the device cannot tell: poll instead   */
    }
    /* The device's drop counter is cumulative since boot, so testing it
     * against zero would mark every answer incomplete for ever once the
     * ring had overflowed even once.  What matters is whether it MOVED
     * since the last ask. */
    if (dropped != impl->last_dropped) {
        impl->last_dropped = dropped;
        if (complete != NULL) {
            *complete = 0;
        }
    }
    /*
     * A created, removed or moved record means the SHAPE of the namespace
     * moved, and the manifest this backend answers stat from was read once
     * at connect.  Refreshing it here is what makes a deleted folder
     * DETECTABLE: without this, ns_find still knows the old path, tk_stat
     * still answers for it, and the window over it can never learn it is
     * gone.
     */
    for (i = 0; i < n; i++) {
        if (strcmp(recs[i].op, "changed") != 0) {
            (void)tiku_ns_load(impl->ns);
            break;
        }
    }
    for (i = 0; i < n && w < max; i++) {
        out[w].id = (uint64_t)recs[i].id;
        /* The op decides what the caller must DO: only a value move can be
         * served by refreshing a row in place; anything else changes the
         * shape of the listing and has to be re-read. */
        if (strcmp(recs[i].op, "created") == 0) {
            out[w].op = TIKU_CH_CREATED;
        } else if (strcmp(recs[i].op, "removed") == 0) {
            out[w].op = TIKU_CH_REMOVED;
        } else if (strcmp(recs[i].op, "moved") == 0) {
            out[w].op = TIKU_CH_MOVED;
        } else {
            out[w].op = TIKU_CH_CHANGED;
        }
        w++;
    }
    return w;
}

static int
tk_path_of_id(tiku_backend_t *b, uint64_t id, char *out, size_t max)
{
    tiku_impl_t *impl = b->impl;
    const tiku_node_t *n = tiku_ns_find_id(impl->ns,
                                                     (unsigned)id);

    if (n == NULL) {
        return -1;
    }
    (void)snprintf(out, max, "%s", n->path);
    return 0;
}

static int
tk_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    tiku_impl_t *impl = b->impl;
    const tiku_node_t *n = tiku_ns_find(impl->ns, path);

    if (n == NULL) {
        /* Not in the manifest.  Ask the device before declaring it absent:
         * the manifest is a snapshot, and a node born since -- a /data file,
         * a runtime-registered driver -- is still a node.  One that the
         * device lists but cannot read is reported as UNKNOWN rather than
         * absent: an entry that cannot be described is not an entry that is
         * not there, and this is what feeds the held tier. */
        int rc = tiku_ns_read(impl->ns, path);

        memset(out, 0, sizeof *out);
        (void)snprintf(out->path, sizeof out->path, "%s", path);
        {
            const char *slash = strrchr(path, '/');

            (void)snprintf(out->name, sizeof out->name, "%s",
                           (slash != NULL) ? slash + 1 : path);
        }
        (void)snprintf(out->facts.meta, sizeof out->facts.meta, "-");
        (void)snprintf(out->facts.req_cap, sizeof out->facts.req_cap, "?");
        out->backend = b;
        if (rc >= 0) {
            out->kind = TIKU_KIND_DEVICE_NODE;
            out->facts.perm = TIKU_P_READ;
            (void)snprintf(out->type, sizeof out->type, "tiku-node/value");
            return 0;
        }
        {
            /* Readable? no.  Listed by its parent? then it exists and is
             * merely indescribable right now. */
            char names[64][64];
            char dir[TIKU_PATH_MAX];
            const char *slash = strrchr(path, '/');
            int k, nls;

            if (slash == NULL || slash == path) {
                (void)snprintf(dir, sizeof dir, "/");
            } else {
                (void)snprintf(dir, sizeof dir, "%.*s",
                               (int)(slash - path), path);
            }
            nls = tiku_ns_ls(impl->ns, dir, names, 64);
            for (k = 0; k < nls; k++) {
                if (slash != NULL && strcmp(names[k], slash + 1) == 0) {
                    out->kind = TIKU_KIND_UNKNOWN;
                    return 0;
                }
            }
        }
        return -1;
    }
    from_node(b, n, out);
    return 0;
}

static int
tk_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn,
        void *ctx)
{
    tiku_impl_t *impl = b->impl;
    const tiku_node_t *kids[256];
    int n, i, count;

    /*
     * The manifest is read once at connect, so serving a listing out of it
     * shows the namespace as it was THEN: a node that appears or goes never
     * shows up, which is liveness quietly absent rather than slow.  So the
     * directory is asked for again here, and the manifest supplies the
     * TYPED detail (descriptor, capability, identity) for the names that
     * come back.  Names the manifest has never heard of are still listed --
     * a node discovered at runtime is a node.
     */
    {
        char names[128][64];
        char kinds[128];
        int live = tiku_ns_ls_kinds(impl->ns, path, names, kinds, 128);

        count = 0;
        if (live > 0) {
            for (i = 0; i < live; i++) {
                tiku_model_t m;
                const tiku_node_t *known;
                char full[TIKU_PATH_MAX];

                if (strcmp(path, "/") == 0) {
                    (void)snprintf(full, sizeof full, "/%s", names[i]);
                } else {
                    (void)snprintf(full, sizeof full, "%s/%s", path,
                                   names[i]);
                }
                known = tiku_ns_find(impl->ns, full);
                if (known != NULL) {
                    from_node(b, known, &m);
                } else {
                    /* Not in the manifest: describe what the listing told
                     * us and leave the rest unknown rather than invented. */
                    memset(&m, 0, sizeof m);
                    (void)snprintf(m.path, sizeof m.path, "%s", full);
                    (void)snprintf(m.name, sizeof m.name, "%s", names[i]);
                    (void)snprintf(m.facts.meta, sizeof m.facts.meta, "-");
                    (void)snprintf(m.facts.req_cap, sizeof m.facts.req_cap,
                                   "?");
                    m.kind = kinds[i] ? TIKU_KIND_VIRTUAL_DIR
                                      : TIKU_KIND_DEVICE_NODE;
                    m.facts.perm = TIKU_P_READ;
                    m.facts.cap_known = 0;
                    m.backend = b;
                    (void)snprintf(m.type, sizeof m.type, "%s",
                                   kinds[i] ? "tiku-dir/namespace"
                                            : "tiku-node/value");
                }
                count++;
                if (fn != NULL && fn(&m, ctx) != 0) {
                    return count;
                }
            }
            return count;
        }
        /* The device could not be asked.  If the manifest has never heard of
         * this path either, the folder is not there -- and saying so is what
         * lets a window close when its folder goes.  Returning a count on
         * every path made that branch unreachable here. */
        n = tiku_ns_children(impl->ns, path, kids, 256);
        if (n <= 0 && tiku_ns_find(impl->ns, path) == NULL) {
            return -1;
        }
        for (i = 0; i < n; i++) {
            tiku_model_t m;
            from_node(b, kids[i], &m);
            count++;
            if (fn != NULL && fn(&m, ctx) != 0) {
                return count;
            }
        }
    }
    /* A dynamic store's contents are data, not policy, so the manifest does
     * not carry them: ask the device. */
    if (n == 0 && strcmp(path, "/data") == 0) {
        char names[64][64];
        int k = tiku_ns_ls(impl->ns, path, names, 64);

        for (i = 0; i < k; i++) {
            tiku_model_t m;
            memset(&m, 0, sizeof m);
            {   /* ls names are shorter than these fields; bound the copy
                 * anyway so the compiler can see it and a long name is
                 * dropped rather than silently renamed. */
                size_t nl = strlen(names[i]);
                if (nl >= sizeof m.name - 8u) { continue; }
                memcpy(m.name, names[i], nl + 1u);
                memcpy(m.path, "/data/", 6u);
                memcpy(m.path + 6u, names[i], nl + 1u);
            }
            m.kind = TIKU_KIND_FILE;
            m.backend = b;
            m.facts.perm = TIKU_P_READ | TIKU_P_WRITE;
            m.facts.cap_known = 1;
            snprintf(m.facts.req_cap, sizeof m.facts.req_cap, "-");
            snprintf(m.facts.meta, sizeof m.facts.meta, "-");
            snprintf(m.type, sizeof m.type, "tiku-file/data");
            count++;
            if (fn != NULL && fn(&m, ctx) != 0) {
                break;
            }
        }
    }
    return count;
}

static int
tk_read(tiku_backend_t *b, const char *path, void *buf, size_t max)
{
    tiku_impl_t *impl = b->impl;
    const tiku_node_t *n;
    size_t len;

    if (tiku_ns_read(impl->ns, path) != 0) {
        return -1;
    }
    n = tiku_ns_find(impl->ns, path);
    if (n == NULL || !n->value_valid) {
        return -1;
    }
    len = strlen(n->value);
    if (len > max) {
        len = max;
    }
    memcpy(buf, n->value, len);
    return (int)len;
}

static int
tk_write(tiku_backend_t *b, const char *path, const void *buf, size_t len,
         char *err, size_t errmax)
{
    tiku_impl_t *impl = b->impl;
    char val[512];
    size_t n = (len < sizeof val - 1u) ? len : sizeof val - 1u;

    memcpy(val, buf, n);
    val[n] = '\0';
    /* The device's own refusal text is the error the user sees: it names the
     * real reason (EACCES from the capability gate, ERANGE from a range) and
     * inventing our own would only obscure it. */
    return tiku_ns_write(impl->ns, path, val, err, errmax);
}

static int
tk_pump(tiku_backend_t *b, int timeout_ms)
{
    tiku_impl_t *impl = b->impl;

    return tiku_ns_pump(impl->ns, timeout_ms);
}

static tiku_store_t *
tk_store(tiku_backend_t *b)
{
    tiku_impl_t *impl = b->impl;

    if (impl->store == NULL) {
        impl->store = tiku_store_sidecar_open(impl->devid);
    }
    return impl->store;
}

static void
tk_close(tiku_backend_t *b)
{
    tiku_impl_t *impl = b->impl;

    if (impl != NULL) {
        /* Flush before the link goes: unplug is the device's unmount, and
         * the arrangement must survive it. */
        if (impl->store != NULL) {
            (void)tiku_store_flush(impl->store);
            tiku_store_free(impl->store);
        }
        tiku_ns_free(impl->ns);
        tiku_session_free(impl->sess);
        free(impl);
    }
    free(b);
}

static const tiku_backend_ops_t TIKU_OPS = {
    /* No setperm: a device node's capability bits are the device's to
     * declare, not the user's to set, and saying so by leaving it out is
     * what makes the permissions grid read-only rather than an edit that
     * fails after the fact (AW-078). */
    "tiku", tk_stat, tk_list, tk_read, tk_write, tk_pump, tk_changes,
    tk_path_of_id, NULL, tk_store, tk_close
};

tiku_backend_t *
tiku_backend_tiku_open(const char *port, int baud)
{
    tiku_backend_t *b;
    tiku_impl_t *impl;
    tiku_tx_t *tx;
    char scan[8][256];

    if (port == NULL) {
        if (tiku_tx_scan_serial(scan, 8) <= 0) {
            return NULL;
        }
        port = scan[0];
    }
    tx = tiku_tx_open_serial(port, baud);
    if (tx == NULL) {
        return NULL;
    }
    b = calloc(1, sizeof *b);
    impl = calloc(1, sizeof *impl);
    if (b == NULL || impl == NULL) {
        tiku_tx_close(tx);
        free(b);
        free(impl);
        return NULL;
    }
    impl->sess = tiku_session_new(tx);
    if (impl->sess == NULL || tiku_session_sync(impl->sess, 4000) != 0) {
        tiku_session_free(impl->sess);
        free(impl);
        free(b);
        return NULL;
    }
    impl->ns = tiku_ns_new(impl->sess);
    if (impl->ns == NULL || tiku_ns_load(impl->ns) < 0) {
        tiku_ns_free(impl->ns);
        tiku_session_free(impl->sess);
        free(impl);
        free(b);
        return NULL;
    }
    if (tiku_ns_truncated(impl->ns)) {
        (void)tiku_ns_complete(impl->ns, 4);
    }
    read_ident_field(impl->ns, "/sys/device/uid",  impl->ident.uid,
                     sizeof impl->ident.uid);
    read_ident_field(impl->ns, "/sys/device/id",   impl->ident.id,
                     sizeof impl->ident.id);
    read_ident_field(impl->ns, "/sys/device/mcu",  impl->ident.mcu,
                     sizeof impl->ident.mcu);
    read_ident_field(impl->ns, "/sys/device/name", impl->ident.name,
                     sizeof impl->ident.name);
    (void)tiku_ident_key(&impl->ident, impl->devid, sizeof impl->devid);
    snprintf(b->devid, sizeof b->devid, "%s", impl->devid);
    b->ops = &TIKU_OPS;
    b->impl = impl;
    return b;
}

/** @brief The identity this backend bound to.  See tiku_model.h. */
const tiku_identity_t *
tiku_backend_tiku_identity(const tiku_backend_t *b)
{
    const tiku_impl_t *impl;

    if (b == NULL || b->ops != &TIKU_OPS) {
        return NULL;
    }
    impl = b->impl;
    return &impl->ident;
}
