/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs.c - Tree walker, path resolver, read/write dispatch, watch
 *
 * The VFS core is intentionally minimal: it resolves slash-separated
 * paths against a static tree of nodes and dispatches to read/write
 * handler functions.  No malloc, no string copies, no inodes.
 *
 * It also owns the watch layer — the namespace as event bus: a
 * fixed table of (node, process) subscriptions, rung automatically
 * on every successful write and explicitly by drivers via
 * tiku_vfs_notify().  See the WATCH section in tiku_vfs.h for the
 * full semantics.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs.h"
#include "tiku_vfs_cache.h"
#include <kernel/process/tiku_process.h>
#include <hal/tiku_cpu.h>
#include <stdio.h>
#include <string.h>   /* memcpy/memset for the dynamic-directory helpers */

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static const tiku_vfs_node_t *vfs_root;

/**
 * @brief One watch subscription: which node rings which process.
 *
 * A slot is free when @ref node is NULL.  Slots live in SRAM —
 * subscriptions are per-boot state, re-established by their owners
 * at init (the rules engine re-arms after every rule mutation).
 */
typedef struct {
    const tiku_vfs_node_t *node;   /**< watched node (NULL = free) */
    struct tiku_process   *proc;   /**< event receiver             */
} tiku_vfs_watch_slot_t;

/**
 * The watch table.  Fixed capacity (TIKU_VFS_WATCH_MAX, default 8);
 * linear scans throughout — at this size a scan is a handful of
 * compares, cheaper than any indexing structure's bookkeeping.
 *
 * Concurrency model: mutation (watch/unwatch) runs in process
 * context inside tiku_atomic_enter()/exit(), so an ISR calling
 * tiku_vfs_notify() can never observe a half-written slot.  Scans
 * (notify) take no lock: processes are cooperative (no preemption
 * between them) and ISR scans see either a fully-valid or a free
 * slot thanks to the masked mutation.
 */
static tiku_vfs_watch_slot_t watch_table[TIKU_VFS_WATCH_MAX];

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compare a path component against a node name
 *
 * Matches the characters in [comp, comp+len) against name.
 * Returns 1 if equal, 0 otherwise.
 */
static int comp_match(const char *comp, size_t len, const char *name)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (name[i] == '\0' || name[i] != comp[i]) {
            return 0;
        }
    }

    return name[len] == '\0';
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Register the root node of the VFS tree.
 *
 * All subsequent resolve/read/write calls walk from this root.
 * The tree is static (built at compile time); init just stores
 * the pointer.
 */
void tiku_vfs_init(const tiku_vfs_node_t *root)
{
    vfs_root = root;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Walk the VFS tree to resolve a slash-separated path.
 *
 * Splits the path into components, descends through directory
 * nodes by matching each component against child names (linear
 * scan per level).  Returns NULL on any mismatch, non-directory
 * intermediate, or NULL root.  Handles leading/trailing/duplicate
 * slashes gracefully.
 */
const tiku_vfs_node_t *tiku_vfs_resolve(const char *path)
{
    const tiku_vfs_node_t *node;
    const char *p;
    const char *comp;
    size_t comp_len;
    uint8_t i;
    int found;

    if (path == NULL || path[0] != '/' || vfs_root == NULL) {
        return NULL;
    }

    node = vfs_root;
    p = path + 1;  /* skip leading '/' */

    /* Root path: "/" or empty after slash */
    if (*p == '\0') {
        return node;
    }

    while (*p != '\0') {
        /* Skip consecutive slashes */
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;  /* trailing slash */
        }

        /* Extract component */
        comp = p;
        while (*p != '/' && *p != '\0') {
            p++;
        }
        comp_len = (size_t)(p - comp);

        /* Current node must be a directory to descend */
        if (node->type != TIKU_VFS_DIR || node->children == NULL) {
            return NULL;
        }

        /* Search children for matching name */
        found = 0;
        for (i = 0; i < node->child_count; i++) {
            if (comp_match(comp, comp_len, node->children[i].name)) {
                node = &node->children[i];
                found = 1;
                break;
            }
        }

        if (!found) {
            return NULL;
        }
    }

    return node;
}

/*---------------------------------------------------------------------------*/
/* DYNAMIC DIRECTORY SUPPORT — runtime children (e.g. the /data file store)  */
/*---------------------------------------------------------------------------*/

/* Resolve the dynamic-directory MOUNT for @p path, returning everything after
 * it in @p name_out.  Used only on the dynamic fallback path (a path that does
 * not resolve to a static node), so the common case pays nothing for it.
 *
 * The parent is the DEEPEST ancestor that is a dynamic directory; the remainder
 * after that slash becomes the child name and MAY itself contain slashes.  The
 * file store treats those as part of one flat name, which the shell/VFS present
 * as virtual sub-folders (path-as-name) -- so /data/logs/jan.txt resolves to the
 * /data mount with child name "logs/jan.txt", while the flat /data/cfg.json
 * still resolves to /data with child "cfg.json" exactly as before. */
static const tiku_vfs_node_t *vfs_parent_of(const char *path,
                                            const char **name_out)
{
    const char *p, *end;
    char pbuf[TIKU_VFS_PATHBUF];
    size_t plen;
    const tiku_vfs_node_t *par;

    if (path == NULL || path[0] != '/') {
        return NULL;
    }
    for (end = path; *end != '\0'; end++) {
        ;                                      /* find the terminator */
    }
    /* Walk back over each '/': the first (deepest) whose prefix resolves to a
     * dynamic dir wins.  (A dynamic child directly at root is not a thing --
     * the root is a static dir -- so stopping above the leading slash is fine;
     * it yields the same -1 the old immediate-parent form did for that case.) */
    for (p = end; p > path; p--) {
        if (*p != '/' || p[1] == '\0') {       /* not a slash / trailing slash */
            continue;
        }
        plen = (size_t)(p - path);
        if (plen == 0) {
            par = vfs_root;
        } else if (plen < sizeof pbuf) {
            memcpy(pbuf, path, plen);
            pbuf[plen] = '\0';
            par = tiku_vfs_resolve(pbuf);
        } else {
            continue;                          /* prefix too long for scratch */
        }
        if (par != NULL && par->dyn != NULL) {
            *name_out = p + 1;
            return par;
        }
    }
    return NULL;
}

/* Read a dynamic child via the parent dir's dyn ops.  -1 if not dynamic. */
static int vfs_dyn_read(const char *path, char *buf, size_t max)
{
    const char *name = NULL;
    const tiku_vfs_node_t *par = vfs_parent_of(path, &name);
    if (par == NULL || par->dyn == NULL || par->dyn->read == NULL) {
        return -1;
    }
    return par->dyn->read(name, buf, max);
}

/* Write/create a dynamic child; rings the parent dir's watchers on success. */
static int vfs_dyn_write(const char *path, const char *data, size_t len)
{
    const char *name = NULL;
    const tiku_vfs_node_t *par = vfs_parent_of(path, &name);
    int rc;
    if (par == NULL || par->dyn == NULL || par->dyn->write == NULL) {
        return -1;
    }
    rc = par->dyn->write(name, data, len);
    if (rc == 0) {
        tiku_vfs_notify(par);   /* watchers on the directory see the change */
    }
    return rc;
}

/* Delete a dynamic child; rings the parent dir's watchers on success. */
static int vfs_dyn_unlink(const char *path)
{
    const char *name = NULL;
    const tiku_vfs_node_t *par = vfs_parent_of(path, &name);
    int rc;
    if (par == NULL || par->dyn == NULL || par->dyn->unlink == NULL) {
        return -1;
    }
    rc = par->dyn->unlink(name);
    if (rc == 0) {
        tiku_vfs_notify(par);   /* watchers on the directory see the change */
    }
    return rc;
}

/* Flag-only sentinels: a dynamic child IS read/write (through its parent's dyn
 * ops), but the const node handlers carry no file identity, so a synthesised
 * list node has no handler to point at.  These mark it read+write so `ls`
 * renders "rw"; they are never invoked — resolve() never returns a synthesised
 * node, so reads/writes take the dyn fallback path, not these. */
static int vfs_dyn_file_rd(char *b, size_t m)        { (void)b; (void)m; return -1; }
static int vfs_dyn_file_wr(const char *b, size_t l)  { (void)b; (void)l; return -1; }

/* Adapter: present each dynamic child to a tiku_vfs_list_fn as a transient
 * node (valid only during the callback).  A name ending in '/' is a virtual
 * sub-folder (path-as-name) and becomes a DIR node; anything else is a FILE. */
typedef struct { tiku_vfs_list_fn cb; void *ctx; } vfs_dyn_list_adapter_t;
static void vfs_dyn_list_thunk(const char *name, void *vad)
{
    vfs_dyn_list_adapter_t *ad = (vfs_dyn_list_adapter_t *)vad;
    tiku_vfs_node_t tmp;
    size_t len = (name != NULL) ? strlen(name) : 0;
    memset(&tmp, 0, sizeof tmp);
    if (len > 0 && name[len - 1] == '/') {
        char dbuf[64];                 /* name without the trailing slash */
        if (len - 1 < sizeof dbuf) {
            memcpy(dbuf, name, len - 1);
            dbuf[len - 1] = '\0';
            tmp.name = dbuf;
        } else {
            tmp.name = name;
        }
        tmp.type = TIKU_VFS_DIR;       /* `ls` re-appends the '/' */
        ad->cb(&tmp, ad->ctx);
    } else {
        tmp.name  = name;
        tmp.type  = TIKU_VFS_FILE;
        tmp.read  = vfs_dyn_file_rd;   /* flag only: ls shows "rw" (see above) */
        tmp.write = vfs_dyn_file_wr;
        ad->cb(&tmp, ad->ctx);
    }
}

/* Delete a file (or other dynamic child) at @p path.  Static nodes have no
 * unlink and return -1; the write path's notify contract applies here too. */
int tiku_vfs_unlink(const char *path)
{
    return vfs_dyn_unlink(path);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Invoke a resolved node's read handler directly.
 *
 * The by-node read path.  Callers that already hold a node pointer
 * skip the tree walk entirely: a watch event delivers the changed
 * node as its payload, and the rules engine and the `watch` command
 * cache the node at arm time.  On the reactive hot path — a read per
 * delivered event — this removes a full path resolution each time.
 *
 * Validation is identical to tiku_vfs_read() (the node must be a
 * readable FILE), so both entry points share one error contract; a
 * NULL @p node (e.g. from a failed resolve) yields -1, which lets
 * tiku_vfs_read() forward an unresolved path through unchanged.
 *
 * @param node  Node to read (NULL tolerated → -1)
 * @param buf   Output buffer
 * @param max   Buffer capacity
 * @return Bytes written, or -1 if @p node is not a readable file
 */
int tiku_vfs_read_node(const tiku_vfs_node_t *node, char *buf, size_t max)
{
    if (node == NULL) {
        return TIKU_VFS_ENOENT;
    }
    if (node->type != TIKU_VFS_FILE || node->read == NULL) {
        return TIKU_VFS_EACCES;   /* exists, but not a readable file */
    }

#if TIKU_VFS_CACHE_ENABLE
    /* Cacheable nodes (a freshness window in the descriptor) route through
     * the read-coalescing cache: a fresh hit skips the handler entirely;
     * a miss samples the handler and stores the rendering. */
    if (node->desc != NULL && node->desc->fresh_ticks > 0u) {
        int hit = tiku_vfs_cache_get(node, buf, max);
        if (hit >= 0) {
            return hit;
        }
        return tiku_vfs_cache_sample(node, buf, max);
    }
#endif

    return node->read(buf, max);
}

/**
 * @brief Resolve a path and invoke the file's read handler.
 *
 * Thin wrapper over tiku_vfs_read_node(): resolve once, then
 * dispatch.  Returns -1 if the path does not resolve to a readable
 * file.
 */
int tiku_vfs_read(const char *path, char *buf, size_t max)
{
    const tiku_vfs_node_t *node = tiku_vfs_resolve(path);
    int rc;
    if (node != NULL) {
        return tiku_vfs_read_node(node, buf, max);   /* static node */
    }
    rc = vfs_dyn_read(path, buf, max);               /* dynamic /data/<file> */
    return (rc < 0) ? TIKU_VFS_ENOENT : rc;          /* neither static nor dynamic → not found */
}

/*---------------------------------------------------------------------------*/
/* TYPED ACCESS — descriptor-driven, machine-facing reads                    */
/*---------------------------------------------------------------------------*/
/*
 * The typed layer never touches the text handlers' contract: a typed
 * read either calls the descriptor's native producer or renders the
 * existing text handler once and decodes the digits.  Untyped nodes
 * are untouched.  All decoding is hand-rolled (no strtol pulled in)
 * so the parse is deterministic and free of libc/locale surprises on
 * the 16-bit target.
 */

/* Short tokens for tiku_vfs_desc_str(); indexed by the matching enum. */
static const char *const vfs_vtype_names[] = {
    "none", "u32", "i32", "bool", "fixed", "str"
};
static const char *const vfs_unit_names[] = {
    "", "bool", "count", "bytes", "s", "ms", "ticks", "Hz",
    "mV", "mA", "C", "mC", "%", "adc", "uJ"
};
static const char *const vfs_fresh_names[] = { "static", "cached", "live" };
static const char *const vfs_ecost_names[] = { "free", "cheap", "periph", "bus" };

#define VFS_NAME_OF(tbl, i)                                                 \
    (((unsigned)(i) < (sizeof(tbl) / sizeof((tbl)[0]))) ? (tbl)[(i)] : "?")

/**
 * @brief Parse a leading integer (optional sign, decimal or 0x hex).
 *
 * Skips leading blanks, reads one signed/unsigned integer, and stops
 * at the first non-digit (e.g. the trailing '\n', or "err").
 *
 * @return 0 and sets the magnitude and sign on success; -1 if no
 *         digits were seen.
 */
static int vfs_parse_num(const char *s, uint32_t *mag, int *neg)
{
    uint32_t acc = 0;
    int any = 0;

    *neg = 0;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '-') {
        *neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        for (;;) {
            char c = *s;
            int d;
            if (c >= '0' && c <= '9') {
                d = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                d = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                d = c - 'A' + 10;
            } else {
                break;
            }
            acc = acc * 16u + (uint32_t)d;
            any = 1;
            s++;
        }
    } else {
        while (*s >= '0' && *s <= '9') {
            acc = acc * 10u + (uint32_t)(*s - '0');
            any = 1;
            s++;
        }
    }

    if (!any) {
        return -1;
    }
    *mag = acc;
    return 0;
}

/** @brief Decode rendered text into @p out per the descriptor's type. */
static int vfs_decode_text(const char *s, const tiku_vfs_desc_t *d,
                           tiku_vfs_val_t *out)
{
    uint32_t mag;
    int neg;

    switch (d->vtype) {
    case TIKU_VFS_T_U32:
    case TIKU_VFS_T_FIXED:
        if (vfs_parse_num(s, &mag, &neg) != 0) {
            return -1;
        }
        out->vtype = d->vtype;
        out->as.u = mag;
        return 0;

    case TIKU_VFS_T_I32:
        if (vfs_parse_num(s, &mag, &neg) != 0) {
            return -1;
        }
        out->vtype = TIKU_VFS_T_I32;
        out->as.i = neg ? -(int32_t)mag : (int32_t)mag;
        return 0;

    case TIKU_VFS_T_BOOL:
        if (vfs_parse_num(s, &mag, &neg) == 0) {
            out->vtype = TIKU_VFS_T_BOOL;
            out->as.u = (mag != 0u) ? 1u : 0u;
            return 0;
        }
        /* Accept on/off, true/false as well as 0/1. */
        if (*s == 'o' || *s == 'O') {
            out->vtype = TIKU_VFS_T_BOOL;
            out->as.u = (s[1] == 'n' || s[1] == 'N') ? 1u : 0u;
            return 0;
        }
        if (*s == 't' || *s == 'T') {
            out->vtype = TIKU_VFS_T_BOOL;
            out->as.u = 1u;
            return 0;
        }
        if (*s == 'f' || *s == 'F') {
            out->vtype = TIKU_VFS_T_BOOL;
            out->as.u = 0u;
            return 0;
        }
        return -1;

    default:
        return -1;   /* STR / NONE: caller uses the text path */
    }
}

/**
 * @brief Read a held node as a decoded typed value.
 *
 * Native producer wins; otherwise render the text handler into a
 * small scratch buffer and decode.  @p out is always zeroed first so
 * a failed read leaves a clean NONE value rather than stale bytes.
 */
int tiku_vfs_read_val_node(const tiku_vfs_node_t *node, tiku_vfs_val_t *out)
{
    const tiku_vfs_desc_t *d;
    char tmp[24];
    int n;

    if (out == NULL) {
        return TIKU_VFS_EINVAL;
    }
    out->vtype = TIKU_VFS_T_NONE;
    out->unit = TIKU_VFS_U_NONE;
    out->scale = 0;
    out->as.u = 0;

    if (node == NULL) {
        return TIKU_VFS_ENOENT;
    }
    if (node->type != TIKU_VFS_FILE) {
        return TIKU_VFS_EACCES;
    }
    if (node->desc == NULL) {
        return TIKU_VFS_ERR;         /* readable, but carries no typed view */
    }
    d = node->desc;
    out->unit = d->unit;
    out->scale = d->scale;

    if (d->read_val != NULL) {
        return d->read_val(out);
    }
    if (node->read == NULL) {
        return TIKU_VFS_EACCES;
    }

    /* Render via the by-node read path so a typed read shares the
     * freshness cache (and decodes the cached text on a hit). */
    n = tiku_vfs_read_node(node, tmp, sizeof(tmp));
    if (n <= 0) {
        return (n < 0) ? n : TIKU_VFS_ERR;   /* propagate a classified code; 0 → generic */
    }
    /* snprintf-style handlers return the would-be length; clamp so the
     * scratch buffer is always a valid, NUL-terminated C string. */
    if ((size_t)n >= sizeof(tmp)) {
        n = (int)sizeof(tmp) - 1;
    }
    tmp[n] = '\0';

    return vfs_decode_text(tmp, d, out);
}

/** @brief Resolve a path, then read it as a decoded typed value. */
int tiku_vfs_read_val(const char *path, tiku_vfs_val_t *out)
{
    return tiku_vfs_read_val_node(tiku_vfs_resolve(path), out);
}

/** @brief Return a node's descriptor (NULL if untyped / node NULL). */
const tiku_vfs_desc_t *tiku_vfs_desc_of(const tiku_vfs_node_t *node)
{
    return (node != NULL) ? node->desc : NULL;
}

/** @brief Render a descriptor as a one-line human/manifest string. */
int tiku_vfs_desc_str(const tiku_vfs_node_t *node, char *buf, size_t max)
{
    const tiku_vfs_desc_t *d;
    int n;

    if (node == NULL || buf == NULL || max == 0) {
        return -1;
    }
    d = node->desc;
    if (d == NULL) {
        return snprintf(buf, max, "untyped\n");
    }

    n = snprintf(buf, max, "%s %s cost=%s fresh=%s",
                 VFS_NAME_OF(vfs_vtype_names, d->vtype),
                 VFS_NAME_OF(vfs_unit_names, d->unit),
                 VFS_NAME_OF(vfs_ecost_names, d->ecost),
                 VFS_NAME_OF(vfs_fresh_names, d->fresh));
    if (n < 0) {
        return -1;
    }
    if ((d->flags & TIKU_VFS_DF_RANGE) && (size_t)n < max) {
        int m = snprintf(buf + n, max - (size_t)n, " [%ld..%ld]",
                         (long)d->vmin, (long)d->vmax);
        if (m > 0) {
            n += m;
        }
    }
    if ((size_t)n < max) {
        int m = snprintf(buf + n, max - (size_t)n, "\n");
        if (m > 0) {
            n += m;
        }
    }
    return n;
}

/** @brief Short, stable name for a status code (see tiku_vfs.h). */
const char *tiku_vfs_strerror(int status)
{
    switch (status) {
    case TIKU_VFS_OK:     return "OK";
    case TIKU_VFS_ERR:    return "ERR";
    case TIKU_VFS_ENOENT: return "ENOENT";
    case TIKU_VFS_EACCES: return "EACCES";
    case TIKU_VFS_EINVAL: return "EINVAL";
    case TIKU_VFS_ERANGE: return "ERANGE";
    case TIKU_VFS_E2BIG:  return "E2BIG";
    case TIKU_VFS_EIO:    return "EIO";
    case TIKU_VFS_EPERM:  return "EPERM";
    default:              return "E?";
    }
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Resolve a path and invoke the file's write handler.
 *
 * Returns -1 if the path does not resolve to a writable file.
 *
 * On success (handler returned 0) the node's watchers are rung via
 * tiku_vfs_notify() — this is the automatic trigger path that makes
 * every shell, BASIC, and network write observable without the
 * writer knowing watchers exist.  Failed writes (handler returned
 * -1) do not notify: the node did not change.
 */
/*---------------------------------------------------------------------------*/
/* CALLER CAPABILITY                                                          */
/*---------------------------------------------------------------------------*/

/* Ambient trust of the channel currently driving the VFS.  ALL by default, so
 * the trusted console / kernel / init path is unaffected; an untrusted channel
 * lowers it via tiku_vfs_caller_cap_set(). */
static tiku_vfs_cap_t vfs_caller_cap = TIKU_VFS_CAP_ALL;

tiku_vfs_cap_t tiku_vfs_caller_cap_set(tiku_vfs_cap_t cap)
{
    tiku_vfs_cap_t prev = vfs_caller_cap;
    vfs_caller_cap = cap;
    return prev;
}

tiku_vfs_cap_t tiku_vfs_caller_cap_get(void)
{
    return vfs_caller_cap;
}

/* True iff the current caller holds every capability bit @p req demands. */
static uint8_t vfs_cap_permitted(tiku_vfs_cap_t req)
{
    return (uint8_t)((req & (tiku_vfs_cap_t)~vfs_caller_cap) == 0u);
}

int tiku_vfs_write(const char *path, const char *data, size_t len)
{
    const tiku_vfs_node_t *node;
    int rc;

    node = tiku_vfs_resolve(path);
    if (node == NULL) {
        /* Dynamic child (create-on-write): mutating the file store needs FS. */
        if (!vfs_cap_permitted(TIKU_VFS_CAP_FS)) {
            return TIKU_VFS_EPERM;
        }
        rc = vfs_dyn_write(path, data, len);
        return (rc < 0) ? TIKU_VFS_ENOENT : rc;  /* no static node, no dynamic store here */
    }
    if (node->type != TIKU_VFS_FILE || node->write == NULL) {
        return TIKU_VFS_EACCES;                  /* exists, but not writable */
    }
    if (!vfs_cap_permitted(node->req_cap)) {
        return TIKU_VFS_EPERM;                    /* mediated: caller lacks capability */
    }

    rc = node->write(data, len);
    if (rc == 0) {
        tiku_vfs_notify(node);
    }
    return rc;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief List the children of a directory node.
 *
 * Resolves the path, verifies it is a directory, then invokes
 * @p callback for each child with name, type, and the caller's
 * context pointer.  Returns -1 if the path is not a directory.
 */
/* List a dynamic directory's children at sub-path @p prefix ("" = the mount
 * root).  Prefers the folder-aware list_dir (virtual sub-folders, path-as-name)
 * and falls back to the flat list at the root of a store that has none. */
static void vfs_list_dynamic(const tiku_vfs_node_t *node, const char *prefix,
                             tiku_vfs_list_fn callback, void *ctx)
{
    vfs_dyn_list_adapter_t ad;
    ad.cb = callback;
    ad.ctx = ctx;
    if (node->dyn->list_dir != NULL) {
        node->dyn->list_dir(prefix, vfs_dyn_list_thunk, &ad);
    } else if (prefix[0] == '\0' && node->dyn->list != NULL) {
        node->dyn->list(vfs_dyn_list_thunk, &ad);
    }
}

int tiku_vfs_list(const char *path, tiku_vfs_list_fn callback, void *ctx)
{
    const tiku_vfs_node_t *node;
    const tiku_vfs_node_t *mount;
    const char *sub = NULL;
    char pbuf[TIKU_VFS_PATHBUF];
    size_t sl;
    uint8_t i;

    node = tiku_vfs_resolve(path);
    if (node != NULL) {
        if (node->type != TIKU_VFS_DIR) {
            return -1;
        }
        for (i = 0; i < node->child_count; i++) {
            callback(&node->children[i], ctx);
        }
        /* A dynamic dir (e.g. /data) also enumerates its runtime children. */
        if (node->dyn != NULL) {
            vfs_list_dynamic(node, "", callback, ctx);
        }
        return 0;
    }

    /* Not a static node: maybe a virtual sub-folder of a dynamic store, where
     * the path components after the mount are part of a flat name (path-as-
     * name).  List the mount with that sub-path as the prefix. */
    mount = vfs_parent_of(path, &sub);
    if (mount == NULL || mount->dyn == NULL || mount->dyn->list_dir == NULL) {
        return -1;
    }
    sl = strlen(sub);
    if (sl + 2 > sizeof pbuf) {
        return -1;
    }
    memcpy(pbuf, sub, sl);
    pbuf[sl]     = '/';                  /* prefix = "<sub-path>/" */
    pbuf[sl + 1] = '\0';
    vfs_list_dynamic(mount, pbuf, callback, ctx);
    return 0;
}

/* Probe callback for tiku_vfs_is_dir(): any child flips the flag. */
static void vfs_isdir_probe(const tiku_vfs_node_t *node, void *ctx)
{
    (void)node;
    *(int *)ctx = 1;
}

int tiku_vfs_is_dir(const char *path)
{
    const tiku_vfs_node_t *node = tiku_vfs_resolve(path);
    const tiku_vfs_node_t *mount;
    const char *sub = NULL;
    char pbuf[TIKU_VFS_PATHBUF];
    size_t sl;
    int found = 0;

    if (node != NULL) {
        return (node->type == TIKU_VFS_DIR) ? 1 : 0;
    }
    mount = vfs_parent_of(path, &sub);
    if (mount == NULL || mount->dyn == NULL) {
        return 0;
    }
    sl = strlen(sub);
    if (sl + 2 > sizeof pbuf) {
        return 0;
    }
    /* An exact file at this path -> a file, not a directory. */
    if (mount->dyn->read != NULL) {
        char probe[1];
        if (mount->dyn->read(sub, probe, sizeof probe) >= 0) {
            return 0;
        }
    }
    memcpy(pbuf, sub, sl);
    pbuf[sl]     = '/';                  /* "<sub-path>/" */
    pbuf[sl + 1] = '\0';
    /* A directory if a mkdir marker exists, or if any child lives under it. */
    if (mount->dyn->read != NULL) {
        char probe[1];
        if (mount->dyn->read(pbuf, probe, sizeof probe) >= 0) {
            return 1;
        }
    }
    if (mount->dyn->list_dir != NULL) {
        vfs_dyn_list_adapter_t ad;
        ad.cb = vfs_isdir_probe;
        ad.ctx = &found;
        /* The thunk forwards each child to vfs_isdir_probe via the adapter. */
        mount->dyn->list_dir(pbuf, vfs_dyn_list_thunk, &ad);
    }
    return found;
}

/*---------------------------------------------------------------------------*/
/* WATCH — change notification on nodes                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Subscribe a process to changes of a FILE node.
 *
 * Resolves the path once, at subscription time: the tree is static,
 * so the resulting node pointer is a stable subscription key and no
 * per-event path walk is ever needed.  The duplicate check makes
 * subscription idempotent — consumers that re-arm wholesale (drop
 * everything, re-subscribe every interest) need no bookkeeping
 * about what was already watched.
 *
 * Table mutation runs inside tiku_atomic_enter()/exit() so a
 * concurrent ISR notify scan can never see a torn slot (node and
 * proc are written as a masked pair).
 *
 * @param path  Absolute path to a FILE node
 * @param p     Receiving process
 * @return Slot index (>= 0), or -1 on bad path, non-FILE node,
 *         NULL process, or full table
 */
int8_t tiku_vfs_watch(const char *path, struct tiku_process *p)
{
    const tiku_vfs_node_t *node;
    int8_t free_slot = -1;
    int8_t i;

    if (p == NULL) {
        return -1;
    }
    node = tiku_vfs_resolve(path);
    if (node == NULL || node->type != TIKU_VFS_FILE) {
        return -1;
    }

    tiku_atomic_enter();
    for (i = 0; i < TIKU_VFS_WATCH_MAX; i++) {
        if (watch_table[i].node == node && watch_table[i].proc == p) {
            tiku_atomic_exit();
            return i;               /* idempotent: already watching */
        }
        if (watch_table[i].node == NULL && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot >= 0) {
        watch_table[free_slot].proc = p;
        watch_table[free_slot].node = node;
    }
    tiku_atomic_exit();

    return free_slot;               /* -1 when the table is full */
}

/**
 * @brief Remove one (path, process) subscription.
 *
 * Clearing @ref node frees the slot; the masked section keeps the
 * clear atomic with respect to ISR notify scans.
 *
 * @param path  The watched path
 * @param p     The subscribed process
 * @return 0 when a subscription was removed, -1 when none matched
 */
int8_t tiku_vfs_unwatch(const char *path, struct tiku_process *p)
{
    const tiku_vfs_node_t *node;
    int8_t rc = -1;
    int8_t i;

    node = tiku_vfs_resolve(path);
    if (node == NULL || p == NULL) {
        return -1;
    }

    tiku_atomic_enter();
    for (i = 0; i < TIKU_VFS_WATCH_MAX; i++) {
        if (watch_table[i].node == node && watch_table[i].proc == p) {
            watch_table[i].node = NULL;
            watch_table[i].proc = NULL;
            rc = 0;
            break;
        }
    }
    tiku_atomic_exit();

    return rc;
}

/**
 * @brief Remove every subscription held by @p p.
 *
 * The re-arm primitive: consumers drop all their watches and
 * re-subscribe from current state, which is simpler and safer than
 * tracking which subscription belonged to which (possibly deleted)
 * interest.
 *
 * @param p  The subscribed process
 */
void tiku_vfs_unwatch_all(struct tiku_process *p)
{
    int8_t i;

    if (p == NULL) {
        return;
    }

    tiku_atomic_enter();
    for (i = 0; i < TIKU_VFS_WATCH_MAX; i++) {
        if (watch_table[i].proc == p) {
            watch_table[i].node = NULL;
            watch_table[i].proc = NULL;
        }
    }
    tiku_atomic_exit();
}

/**
 * @brief Ring the watchers of @p node.
 *
 * Posts TIKU_EVENT_VFS (data = the node pointer) to every process
 * subscribed to @p node.  ISR-safe: the scan is lock-free (see the
 * watch_table concurrency note) and tiku_process_post() is itself
 * ISR-safe.  Events are not coalesced — each trigger posts one
 * event per watcher, and the receiver reads the node for the
 * current value, so several queued events for one node degrade to
 * harmless re-reads.
 *
 * Called automatically by tiku_vfs_write() on success; drivers
 * whose node values change without a write (GPIO edges, sensor
 * thresholds) call it explicitly.
 *
 * @param node  The node that changed
 */
void tiku_vfs_notify(const tiku_vfs_node_t *node)
{
    int8_t i;

    if (node == NULL) {
        return;
    }

#if TIKU_VFS_CACHE_ENABLE
    /* A change means any cached rendering is stale -- drop it before
     * waking watchers, so the re-read they do sees the new value. */
    tiku_vfs_cache_invalidate(node);
#endif

    for (i = 0; i < TIKU_VFS_WATCH_MAX; i++) {
        if (watch_table[i].node == node) {
            tiku_process_post(watch_table[i].proc, TIKU_EVENT_VFS,
                              (tiku_event_data_t)(uintptr_t)node);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* INTROSPECTION — the namespace observes itself                             */
/*---------------------------------------------------------------------------*/
/*
 * Read-only views over the two pieces of private VFS state — the
 * watch table and the static tree — so a /sys node can render them
 * (see kernel/vfs/tree/tiku_vfs_tree_watch.c).  All run in process
 * context on a cold path (a human cats a file); none lock, matching
 * the notify-scan reasoning: the cooperative scheduler gives a
 * handler a yield-free run, and tree-node pointers are stable for
 * the life of the system, so a snapshot cannot tear under it.
 */

/**
 * @brief Count the watch slots currently in use.
 *
 * @return Used slots in [0, TIKU_VFS_WATCH_MAX]; free = MAX - used.
 */
uint8_t tiku_vfs_watch_used(void)
{
    uint8_t i, used = 0;

    for (i = 0; i < TIKU_VFS_WATCH_MAX; i++) {
        if (watch_table[i].node != NULL) {
            used++;
        }
    }

    return used;
}

/**
 * @brief Fetch the contents of one watch slot.
 *
 * Lets an observability node show who watches what without exposing
 * the table type.  The returned node pointer is stable (static
 * tree); the process pointer is valid for the caller's yield-free
 * run.
 *
 * @param i     Slot index in [0, TIKU_VFS_WATCH_MAX)
 * @param node  Out: the watched node (may be NULL to ignore)
 * @param proc  Out: the subscribed process (may be NULL to ignore)
 * @return 0 if slot @p i is in use (outputs written), -1 if the
 *         slot is free or @p i is out of range
 */
int8_t tiku_vfs_watch_get(uint8_t i, const tiku_vfs_node_t **node,
                          struct tiku_process **proc)
{
    if (i >= TIKU_VFS_WATCH_MAX || watch_table[i].node == NULL) {
        return -1;
    }
    if (node != NULL) {
        *node = watch_table[i].node;
    }
    if (proc != NULL) {
        *proc = watch_table[i].proc;
    }

    return 0;
}

/**
 * @brief Depth-first search for @p target, building its path.
 *
 * Appends "/<child-name>" at each level into @p buf; on reaching
 * @p target, NUL-terminates and returns the length.  Length is
 * computed even past @p max (snprintf-style), but writes are
 * bounds-clamped so the buffer never overflows.
 *
 * @return Path length on success, -1 if @p target is not under @p dir
 */
static int path_find(const tiku_vfs_node_t *dir,
                     const tiku_vfs_node_t *target,
                     char *buf, size_t max, int pos)
{
    uint8_t i;

    for (i = 0; i < dir->child_count; i++) {
        const tiku_vfs_node_t *c = &dir->children[i];
        const char *n = c->name;
        int p = pos;

        if ((size_t)p < max) {
            buf[p] = '/';
        }
        p++;
        while (*n != '\0') {
            if ((size_t)p < max) {
                buf[p] = *n;
            }
            p++;
            n++;
        }

        if (c == target) {
            buf[((size_t)p < max) ? (size_t)p : (max - 1)] = '\0';
            return p;
        }
        if (c->type == TIKU_VFS_DIR && c->children != NULL) {
            int r = path_find(c, target, buf, max, p);
            if (r >= 0) {
                return r;
            }
        }
    }

    return -1;
}

/**
 * @brief Reverse-resolve a node pointer to its absolute path.
 *
 * The inverse of tiku_vfs_resolve(): the tree has no parent links,
 * so this DFS-searches from the root for the node's address and
 * reconstructs the path on the way down.  O(tree size), intended
 * for cold observability reads (e.g. /sys/watch), not a hot path.
 *
 * @param node  Node to name (must be in the tree)
 * @param buf   Output buffer (>= TIKU_VFS_PATH_MAX recommended)
 * @param max   Buffer capacity
 * @return Path length, or -1 if not found / bad args.  The root
 *         resolves to "/".
 */
int tiku_vfs_path_of(const tiku_vfs_node_t *node, char *buf, size_t max)
{
    if (node == NULL || vfs_root == NULL || buf == NULL || max == 0) {
        return -1;
    }
    if (node == vfs_root) {
        if (max < 2) {
            buf[0] = '\0';
            return -1;
        }
        buf[0] = '/';
        buf[1] = '\0';
        return 1;
    }

    return path_find(vfs_root, node, buf, max, 0);
}

/** @brief Recursive node counter (this node + all descendants). */
static uint16_t count_rec(const tiku_vfs_node_t *n)
{
    uint16_t total = 1;
    uint8_t i;

    if (n->type == TIKU_VFS_DIR && n->children != NULL) {
        for (i = 0; i < n->child_count; i++) {
            total = (uint16_t)(total + count_rec(&n->children[i]));
        }
    }

    return total;
}

/**
 * @brief Total nodes in the tree (dirs + files), counted live.
 * @return Node count, or 0 before tiku_vfs_init().
 */
uint16_t tiku_vfs_count(void)
{
    return (vfs_root != NULL) ? count_rec(vfs_root) : 0;
}

/** @brief Recursive max-depth (a leaf is depth 1). */
static uint8_t depth_rec(const tiku_vfs_node_t *n)
{
    uint8_t i, d, deepest = 0;

    if (n->type == TIKU_VFS_DIR && n->children != NULL) {
        for (i = 0; i < n->child_count; i++) {
            d = depth_rec(&n->children[i]);
            if (d > deepest) {
                deepest = d;
            }
        }
    }

    return (uint8_t)(1 + deepest);
}

/**
 * @brief Deepest path in the tree, in components (root alone = 1).
 * @return Max depth, or 0 before tiku_vfs_init().
 */
uint8_t tiku_vfs_depth(void)
{
    return (vfs_root != NULL) ? depth_rec(vfs_root) : 0;
}

/*---------------------------------------------------------------------------*/
/* MANIFEST — one-read, machine-readable dump of the static namespace        */
/*---------------------------------------------------------------------------*/
/*
 * Render every static node as one tab-separated line so an external agent can
 * learn the device's capabilities in a single read instead of walking it with
 * ls/cat.  Five tab-separated columns:  path  type  perms  meta  cap
 * (type = d|f; perms = rw|r-|-w|--; meta is "-" for an untyped node, else the
 * packed descriptor "vtype,unit,fresh,cost[,lo..hi]"; cap is the capability a
 * writer must hold -- "-" (open), "hw", "sys", "fs", "net" -- so the whole
 * write-access policy is enumerable in one read).  Dynamic directories
 * (/data) are
 * listed but their runtime children are NOT walked -- those are data, not
 * capability metadata (use `ls` for them).  Only node metadata is touched, so
 * a manifest read costs nothing and never samples a live sensor.
 */
typedef struct {
    char  *out;
    size_t max;
    size_t off;   /* running length; may exceed max (snprintf-style truncation) */
} vfs_manifest_sink_t;

/* Short token for a node's required write capability (the manifest's 5th
 * column), so the whole access-control policy is enumerable from the same
 * namespace it governs: `cat /sys/vfs/manifest` shows who may write what. */
static const char *vfs_cap_name(tiku_vfs_cap_t c)
{
    switch (c) {
    case TIKU_VFS_CAP_NONE: return "-";
    case TIKU_VFS_CAP_HW:   return "hw";
    case TIKU_VFS_CAP_SYS:  return "sys";
    case TIKU_VFS_CAP_FS:   return "fs";
    case TIKU_VFS_CAP_NET:  return "net";
    default:                return "cap";   /* combined / other mask */
    }
}

static void manifest_line(vfs_manifest_sink_t *s, const char *path,
                          const tiku_vfs_node_t *n)
{
    const tiku_vfs_desc_t *d = n->desc;
    const char *perm = (n->read && n->write) ? "rw"
                     : n->read               ? "r-"
                     : n->write              ? "-w"
                     :                         "--";
    char   meta[48];   /* packed descriptor: "vtype,unit,fresh,cost[,lo..hi]" */
    size_t room = (s->off < s->max) ? (s->max - s->off) : 0u;
    char  *dst  = s->out + ((s->off < s->max) ? s->off : s->max);
    int    m;

    /* Pack the descriptor into one field so untyped nodes (the majority) cost
     * a single "-" instead of five columns -- keeps the whole manifest inside a
     * typical read buffer. */
    if (d == NULL) {
        meta[0] = '-';
        meta[1] = '\0';
    } else if (d->flags & TIKU_VFS_DF_RANGE) {
        (void)snprintf(meta, sizeof meta, "%s,%s,%s,%s,%ld..%ld",
                       VFS_NAME_OF(vfs_vtype_names, d->vtype),
                       VFS_NAME_OF(vfs_unit_names,  d->unit),
                       VFS_NAME_OF(vfs_fresh_names, d->fresh),
                       VFS_NAME_OF(vfs_ecost_names, d->ecost),
                       (long)d->vmin, (long)d->vmax);
    } else {
        (void)snprintf(meta, sizeof meta, "%s,%s,%s,%s",
                       VFS_NAME_OF(vfs_vtype_names, d->vtype),
                       VFS_NAME_OF(vfs_unit_names,  d->unit),
                       VFS_NAME_OF(vfs_fresh_names, d->fresh),
                       VFS_NAME_OF(vfs_ecost_names, d->ecost));
    }

    m = snprintf(dst, room, "%s\t%c\t%s\t%s\t%s\n",
                 path, (n->type == TIKU_VFS_DIR) ? 'd' : 'f', perm, meta,
                 vfs_cap_name(n->req_cap));
    if (m > 0) {
        s->off += (size_t)m;
    }
}

static void manifest_rec(vfs_manifest_sink_t *s, const tiku_vfs_node_t *node,
                         char *path, size_t pathcap, size_t pathlen)
{
    uint8_t i;

    if (node->children == NULL) {
        return;   /* leaf, or a dynamic dir: nothing static to descend */
    }
    for (i = 0; i < node->child_count; i++) {
        const tiku_vfs_node_t *c = &node->children[i];
        size_t nlen = strlen(c->name);
        size_t clen = pathlen;

        if (pathlen + 1u + nlen < pathcap) {           /* append "/name" */
            path[pathlen] = '/';
            memcpy(path + pathlen + 1u, c->name, nlen);
            clen = pathlen + 1u + nlen;
            path[clen] = '\0';
        }
        manifest_line(s, path, c);
        if (c->type == TIKU_VFS_DIR && c->children != NULL) {
            manifest_rec(s, c, path, pathcap, clen);
        }
        path[pathlen] = '\0';                          /* pop back */
    }
}

int tiku_vfs_manifest(char *buf, size_t max)
{
    vfs_manifest_sink_t s;
    char path[TIKU_VFS_PATH_MAX];
    int  m;

    s.out = buf;
    s.max = max;
    s.off = 0;
    if (buf != NULL && max > 0u) {
        buf[0] = '\0';
    }
    if (vfs_root == NULL) {
        return 0;
    }

    /* Self-describing header row. */
    m = snprintf(buf, max,
                 "# path\ttype\tperms\tmeta(vtype,unit,fresh,cost[,lo..hi])\n");
    if (m > 0) {
        s.off += (size_t)m;
    }

    path[0] = '\0';
    manifest_rec(&s, vfs_root, path, sizeof path, 0u);
    return (int)s.off;
}
