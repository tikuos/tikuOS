/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ns.c - manifest parsing and the mirrored namespace.
 *
 * Manifest lines are "path\tkind\tperm\tmeta\tcap"; everything the UI needs to
 * decide what a node IS comes from that one read, so windows never guess a
 * node's type or writability.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_ns.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define NS_MANIFEST_PATH  "/sys/vfs/manifest"
#define NS_READ_BUF       (64u * 1024u)
#define NS_PENDING_MAX    64

struct tiku_ns {
    tiku_session_t *s;
    tiku_node_t    *nodes;
    int                  count;
    int                  cap;
    int                  truncated;       /* manifest cut by the buffer */
    int                  has_push;        /* device answered `sub`     */
    int                  push_probed;
    char                 pending[NS_PENDING_MAX][TIKU_NS_PATH_MAX];
    int                  npending;
};

/** @brief Queue a rung path; the pump re-reads it after the current command. */
static void
ns_notify(const char *path, void *ctx)
{
    tiku_ns_t *ns = ctx;
    int i;

    for (i = 0; i < ns->npending; i++) {
        if (strcmp(ns->pending[i], path) == 0) {
            return;                       /* already queued */
        }
    }
    if (ns->npending < NS_PENDING_MAX) {
        snprintf(ns->pending[ns->npending], TIKU_NS_PATH_MAX, "%s", path);
        ns->npending++;
    }
}

tiku_ns_t *
tiku_ns_new(tiku_session_t *s)
{
    tiku_ns_t *ns;

    if (s == NULL) {
        return NULL;
    }
    ns = calloc(1, sizeof *ns);
    if (ns == NULL) {
        return NULL;
    }
    ns->s = s;
    ns->cap = 256;
    ns->nodes = calloc((size_t)ns->cap, sizeof *ns->nodes);
    if (ns->nodes == NULL) {
        free(ns);
        return NULL;
    }
    tiku_session_on_notify(s, ns_notify, ns);
    return ns;
}

void
tiku_ns_free(tiku_ns_t *ns)
{
    if (ns == NULL) {
        return;
    }
    free(ns->nodes);
    free(ns);
}

static tiku_node_t *
ns_grow(tiku_ns_t *ns)
{
    if (ns->count == ns->cap) {
        int ncap = ns->cap * 2;
        tiku_node_t *p = realloc(ns->nodes,
                                      (size_t)ncap * sizeof *ns->nodes);
        if (p == NULL) {
            return NULL;
        }
        memset(p + ns->cap, 0, (size_t)(ncap - ns->cap) * sizeof *p);
        ns->nodes = p;
        ns->cap = ncap;
    }
    return &ns->nodes[ns->count++];
}

/**
 * @brief Copy up to the next tab or end of line; returns the field's end.
 *
 * The device ends lines with CRLF, so CR terminates a field too -- otherwise
 * it rides along in the last column and every value compares unequal.
 */
static const char *
field(const char *p, char *out, size_t max)
{
    size_t n = 0;

    while (*p != '\t' && *p != '\n' && *p != '\r' && *p != '\0') {
        if (n + 1u < max) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
    return p;
}

/** @brief '/' components in a path; "/" is depth 0. */
static int
path_depth(const char *p)
{
    int d = 0;

    for (; *p != '\0'; p++) {
        if (*p == '/' && p[1] != '\0') {
            d++;
        }
    }
    return d;
}

/**
 * @brief Cache the last path component for display.
 *
 * A name longer than the field is truncated deliberately: the full path
 * stays authoritative, and only the label a row shows is shortened.
 */
static void
set_name(tiku_node_t *n)
{
    const char *slash = strrchr(n->path, '/');
    const char *base = (slash != NULL && slash[1] != '\0') ? slash + 1
                                                           : n->path;
    size_t len = strlen(base);

    if (len >= sizeof n->name) {
        len = sizeof n->name - 1u;
    }
    memcpy(n->name, base, len);
    n->name[len] = '\0';
}

int
tiku_ns_load(tiku_ns_t *ns)
{
    char *buf;
    const char *p;
    int rc;

    if (ns == NULL) {
        return -1;
    }
    buf = malloc(NS_READ_BUF);
    if (buf == NULL) {
        return -1;
    }
    rc = tiku_session_cmd(ns->s, "cat " NS_MANIFEST_PATH, buf,
                               NS_READ_BUF, 15000);
    if (rc < 0 || strstr(buf, "cannot read") != NULL) {
        free(buf);
        return -1;
    }
    ns->count = 0;
    /* The device renders the manifest into the shell's read buffer, so a
     * large tree arrives cut mid-line.  Note it: the tail is completed by
     * ls-walking rather than silently missing. */
    ns->truncated = (rc > 0 && buf[rc - 1] != '\n');
    for (p = buf; *p != '\0'; ) {
        tiku_node_t *n;
        char kind[8], perm[8];

        if (*p == '\n' || *p == '\r') {
            p++;
            continue;
        }
        if (*p == '#') {                  /* the manifest's column header */
            while (*p != '\0' && *p != '\n') { p++; }
            continue;
        }
        n = ns_grow(ns);
        if (n == NULL) {
            break;
        }
        memset(n, 0, sizeof *n);
        p = field(p, n->path, sizeof n->path);
        if (*p == '\t') { p++; }
        p = field(p, kind, sizeof kind);
        if (*p == '\t') { p++; }
        p = field(p, perm, sizeof perm);
        if (*p == '\t') { p++; }
        p = field(p, n->meta, sizeof n->meta);
        if (*p == '\t') { p++; }
        p = field(p, n->cap, sizeof n->cap);
        if (*p == '\t') { p++; }
        {
            /* rev 3 added the identity column.  An older device simply has
             * no sixth field, and leaving it zero is what tells the caller
             * to fall back rather than trust a made-up id. */
            char idtxt[16];

            p = field(p, idtxt, sizeof idtxt);
            n->id = (idtxt[0] != '\0')
                        ? (unsigned)strtoul(idtxt, NULL, 16) : 0u;
        }
        while (*p != '\0' && *p != '\n') { p++; }

        n->is_dir = (kind[0] == 'd');
        n->perm = 0u;
        if (perm[0] == 'r') { n->perm |= TIKU_NS_P_READ; }
        if (perm[1] == 'w') { n->perm |= TIKU_NS_P_WRITE; }
        n->depth = path_depth(n->path);
        set_name(n);
    }
    /* A cut final line yields a node with no capability field; it is a
     * fragment, not a node. */
    if (ns->truncated && ns->count > 0) {
        ns->count--;
    }
    free(buf);
    return ns->count;
}

int
tiku_ns_events(tiku_ns_t *ns, tiku_ns_change_t *out, int max,
                    unsigned *dropped)
{
    char buf[1024];
    char *line, *save;
    int n = 0, rc;

    if (dropped != NULL) {
        *dropped = 0u;
    }
    if (ns == NULL) {
        return -1;
    }
    rc = tiku_session_cmd(ns->s, "cat /sys/vfs/events", buf,
                               sizeof buf, 4000);
    if (rc <= 0) {
        /* No such node: an older device that cannot say what changed.  The
         * caller must poll, and telling it apart from "nothing happened" is
         * the whole point of the negative return. */
        return -1;
    }
    buf[rc] = '\0';
    for (line = strtok_r(buf, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        if (line[0] == '#') {
            unsigned d = 0u, drained = 0u;

            if (sscanf(line, "# %u drained, %u dropped", &drained, &d) == 2 &&
                dropped != NULL) {
                *dropped = d;
            }
            continue;
        }
        {
            char op[12];
            unsigned id = 0u, seq = 0u;

            if (sscanf(line, "%11s %x %u", op, &id, &seq) == 3) {
                if (out != NULL && n < max) {
                    snprintf(out[n].op, sizeof out[n].op, "%s", op);
                    out[n].id = id;
                    out[n].seq = seq;
                    n++;
                } else {
                    /* The read already DRAINED these from the device, so a
                     * record that does not fit is gone for good.  Counting
                     * it as a drop is what tells the caller its picture is
                     * incomplete -- silently returning the first few would
                     * be the same silent loss the device's own drop counter
                     * exists to prevent. */
                    if (dropped != NULL) {
                        (*dropped)++;
                    }
                }
            }
        }
    }
    return (n > max) ? max : n;
}

const tiku_node_t *
tiku_ns_find_id(const tiku_ns_t *ns, unsigned id)
{
    int i;

    if (ns == NULL || id == 0u) {
        return NULL;
    }
    for (i = 0; i < ns->count; i++) {
        if (ns->nodes[i].id == id) {
            return &ns->nodes[i];
        }
    }
    return NULL;
}

int
tiku_ns_count(const tiku_ns_t *ns)
{
    return (ns != NULL) ? ns->count : 0;
}

const tiku_node_t *
tiku_ns_at(const tiku_ns_t *ns, int i)
{
    if (ns == NULL || i < 0 || i >= ns->count) {
        return NULL;
    }
    return &ns->nodes[i];
}

static tiku_node_t *
ns_find_mut(tiku_ns_t *ns, const char *path)
{
    int i;

    if (ns == NULL || path == NULL) {
        return NULL;
    }
    for (i = 0; i < ns->count; i++) {
        if (strcmp(ns->nodes[i].path, path) == 0) {
            return &ns->nodes[i];
        }
    }
    return NULL;
}

const tiku_node_t *
tiku_ns_find(const tiku_ns_t *ns, const char *path)
{
    return ns_find_mut((tiku_ns_t *)ns, path);
}

int
tiku_ns_children(const tiku_ns_t *ns, const char *path,
                      const tiku_node_t **out, int max)
{
    size_t plen;
    int i, n = 0;
    int root;

    if (ns == NULL || path == NULL || out == NULL) {
        return 0;
    }
    root = (path[0] == '/' && path[1] == '\0');
    plen = strlen(path);
    for (i = 0; i < ns->count && n < max; i++) {
        const tiku_node_t *c = &ns->nodes[i];
        const char *rest;

        if (root) {
            /* Root's children are the depth-1 paths: "/sys", "/dev", ... */
            if (c->depth == 1 && c->path[0] == '/') {
                out[n++] = c;
            }
            continue;
        }
        if (strncmp(c->path, path, plen) != 0 || c->path[plen] != '/') {
            continue;
        }
        rest = c->path + plen + 1;
        if (*rest != '\0' && strchr(rest, '/') == NULL) {
            out[n++] = c;
        }
    }
    return n;
}

int
tiku_ns_ls_kinds(tiku_ns_t *ns, const char *path, char out[][64],
                      char *is_dir, int max)
{
    char buf[8192];
    char cmd[TIKU_NS_PATH_MAX + 8];
    const char *p;
    int n = 0;

    if (ns == NULL || path == NULL) {
        return -1;
    }
    snprintf(cmd, sizeof cmd, "ls %s", path);
    if (tiku_session_cmd(ns->s, cmd, buf, sizeof buf, 6000) < 0) {
        return -1;
    }
    if (strstr(buf, "cannot list") != NULL) {
        return 0;
    }
    /* `ls` prints "  <perm> <name>" per entry, directories with a slash. */
    for (p = buf; *p != '\0' && n < max; ) {
        const char *e = strchr(p, '\n');
        const char *q = p;
        size_t len;
        char last[64];

        if (e == NULL) {
            e = p + strlen(p);
        }
        while (q < e && *q == ' ') { q++; }
        while (q < e && *q != ' ') { q++; }   /* skip the perm column */
        while (q < e && *q == ' ') { q++; }
        len = (size_t)(e - q);
        while (len > 0u && q[len - 1u] == '\r') {
            len--;
        }
        if (len > 0u && q[len - 1u] == '/') {   /* ls marks dirs this way */
            if (is_dir != NULL) { is_dir[n] = 1; }
            len--;
        } else if (is_dir != NULL) {
            is_dir[n] = 0;
        }
        if (len > 0u && len < sizeof last) {
            memcpy(last, q, len);
            last[len] = '\0';
            snprintf(out[n], 64, "%s", last);
            n++;
        }
        p = (*e == '\0') ? e : e + 1;
    }
    return n;
}

int
tiku_ns_ls(tiku_ns_t *ns, const char *path, char out[][64], int max)
{
    return tiku_ns_ls_kinds(ns, path, out, NULL, max);
}

int
tiku_ns_read(tiku_ns_t *ns, const char *path)
{
    tiku_node_t *n = ns_find_mut(ns, path);
    char cmd[TIKU_NS_PATH_MAX + 8];
    char buf[TIKU_VAL_MAX];
    int rc;

    if (n == NULL || (n->perm & TIKU_NS_P_READ) == 0u) {
        return -1;
    }
    snprintf(cmd, sizeof cmd, "cat %s", path);
    rc = tiku_session_cmd(ns->s, cmd, buf, sizeof buf, 6000);
    if (rc < 0 || strstr(buf, "cannot read") != NULL) {
        n->value_valid = 0;
        return -1;
    }
    if (strcmp(n->value, buf) != 0) {
        snprintf(n->value, sizeof n->value, "%s", buf);
        n->generation++;
    }
    n->value_valid = 1;
    return 0;
}

int
tiku_ns_write(tiku_ns_t *ns, const char *path, const char *value,
                   char *err, size_t errmax)
{
    char cmd[TIKU_NS_PATH_MAX + TIKU_VAL_MAX + 16];
    char buf[512];
    int rc;

    if (ns == NULL || path == NULL || value == NULL) {
        return -1;
    }
    snprintf(cmd, sizeof cmd, "write %s %s", path, value);
    rc = tiku_session_cmd(ns->s, cmd, buf, sizeof buf, 20000);
    if (rc < 0) {
        return -1;
    }
    /* The shell prints "write: cannot write '<path>' (<reason>)" on refusal,
     * and nothing at all on success. */
    if (strstr(buf, "cannot write") != NULL) {
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "%s", buf);
        }
        return -1;
    }
    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    (void)tiku_ns_read(ns, path);
    return 0;
}

int
tiku_ns_subscribe(tiku_ns_t *ns, const char *path)
{
    tiku_node_t *n = ns_find_mut(ns, path);
    char cmd[TIKU_NS_PATH_MAX + 16];
    char buf[256];

    if (n == NULL) {
        return -1;
    }
    if (ns->push_probed && !ns->has_push) {
        n->subscribed = 1;                 /* poll-driven refresh */
        return 1;
    }
    snprintf(cmd, sizeof cmd, "sub add %s", path);
    if (tiku_session_cmd(ns->s, cmd, buf, sizeof buf, 4000) < 0) {
        return -1;
    }
    ns->push_probed = 1;
    if (strstr(buf, "Unknown command") != NULL ||
        strstr(buf, "usage") != NULL) {
        ns->has_push = 0;
        n->subscribed = 1;
        return 1;
    }
    ns->has_push = 1;
    n->subscribed = 1;
    return 0;
}

int
tiku_ns_unsubscribe(tiku_ns_t *ns, const char *path)
{
    tiku_node_t *n = ns_find_mut(ns, path);
    char cmd[TIKU_NS_PATH_MAX + 16];

    if (n == NULL) {
        return -1;
    }
    n->subscribed = 0;
    if (!ns->has_push) {
        return 0;
    }
    snprintf(cmd, sizeof cmd, "sub del %s", path);
    return (tiku_session_cmd(ns->s, cmd, NULL, 0u, 4000) < 0) ? -1 : 0;
}

int
tiku_ns_truncated(const tiku_ns_t *ns)
{
    return (ns != NULL) ? ns->truncated : 0;
}

/**
 * @brief Join a parent path and a child name.
 *
 * Refuses rather than truncates: a shortened path would name a different
 * node, which is worse than omitting a deep one.
 */
static int
ns_join(char *out, size_t max, const char *parent, const char *name)
{
    int root = (parent[0] == '/' && parent[1] == '\0');
    size_t need = strlen(parent) + strlen(name) + 2u;

    if (need > max) {
        return -1;
    }
    /* Built by hand rather than snprintf: the length is already proven, and
     * a formatter here only teaches the compiler to doubt it. */
    if (root) {
        out[0] = '/';
        memcpy(out + 1, name, strlen(name) + 1u);
    } else {
        size_t pl = strlen(parent);
        memcpy(out, parent, pl);
        out[pl] = '/';
        memcpy(out + pl + 1u, name, strlen(name) + 1u);
    }
    return 0;
}

/** @brief Append a node discovered by ls, with metadata left unknown. */
static tiku_node_t *
ns_add_walked(tiku_ns_t *ns, const char *parent, const char *name,
              int is_dir)
{
    tiku_node_t *n;
    char path[TIKU_NS_PATH_MAX];

    if (ns_join(path, sizeof path, parent, name) != 0) {
        return NULL;
    }
    if (ns_find_mut(ns, path) != NULL) {
        return NULL;                      /* the manifest already had it */
    }
    n = ns_grow(ns);
    if (n == NULL) {
        return NULL;
    }
    memset(n, 0, sizeof *n);
    snprintf(n->path, sizeof n->path, "%s", path);
    snprintf(n->meta, sizeof n->meta, "-");
    snprintf(n->cap, sizeof n->cap, "?");
    n->is_dir = is_dir;
    n->perm = TIKU_NS_P_READ;           /* refined by the first read */
    n->depth = path_depth(n->path);
    set_name(n);
    return n;
}

/**
 * @brief Recursive half of the completion walk.
 *
 * `ls` marks directories with a trailing slash, which tiku_ns_ls strips,
 * so kind is re-derived by asking whether the entry lists as a directory.
 */
static int
ns_walk(tiku_ns_t *ns, const char *path, int depth, int max_depth)
{
    char names[64][64];
    char kinds[64];
    int n, i, added = 0;

    if (depth > max_depth) {
        return 0;
    }
    n = tiku_ns_ls_kinds(ns, path, names, kinds, 64);
    if (n < 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (ns_add_walked(ns, path, names[i], kinds[i]) != NULL) {
            added++;
        }
    }
    for (i = 0; i < n; i++) {
        if (kinds[i]) {
            char child[TIKU_NS_PATH_MAX];
            if (ns_join(child, sizeof child, path, names[i]) == 0) {
                added += ns_walk(ns, child, depth + 1, max_depth);
            }
        }
    }
    return added;
}

int
tiku_ns_complete(tiku_ns_t *ns, int max_depth)
{
    if (ns == NULL) {
        return -1;
    }
    return ns_walk(ns, "/", 0, (max_depth > 0) ? max_depth : 4);
}

int
tiku_ns_has_push(const tiku_ns_t *ns)
{
    return (ns != NULL) ? ns->has_push : 0;
}

int
tiku_ns_pump(tiku_ns_t *ns, int timeout_ms)
{
    int i, refreshed = 0;

    if (ns == NULL) {
        return -1;
    }
    if (tiku_session_poll(ns->s, timeout_ms) < 0) {
        return -1;
    }
    /* Re-read outside the notify callback: a handler must never re-enter the
     * session it was dispatched from. */
    for (i = 0; i < ns->npending; i++) {
        if (tiku_ns_read(ns, ns->pending[i]) == 0) {
            refreshed++;
        }
    }
    ns->npending = 0;

    if (!ns->has_push) {
        /* No push on this firmware: refresh what the UI subscribed to. */
        for (i = 0; i < ns->count; i++) {
            if (ns->nodes[i].subscribed &&
                tiku_ns_read(ns, ns->nodes[i].path) == 0) {
                refreshed++;
            }
        }
    }
    return refreshed;
}
