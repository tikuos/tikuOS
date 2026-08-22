/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vdir.c - merging several real directories into one, by priority.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_vdir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void
tiku_vdir_init(tiku_vdir_t *v)
{
    if (v != NULL) {
        memset(v, 0, sizeof *v);
    }
}

int
tiku_vdir_add(tiku_vdir_t *v, const char *path)
{
    if (v == NULL || path == NULL || path[0] == '\0' ||
        v->nsrc >= TIKU_VDIR_MAX_SRC) {
        return -1;
    }
    snprintf(v->src[v->nsrc], sizeof v->src[0], "%s", path);
    v->nsrc++;
    return 0;
}

int
tiku_vdir_load(tiku_vdir_t *v, const char *deffile)
{
    FILE *f;
    char line[TIKU_PATH_MAX];

    if (v == NULL || deffile == NULL) {
        return -1;
    }
    f = fopen(deffile, "r");
    if (f == NULL) {
        return -1;
    }
    tiku_vdir_init(v);
    snprintf(v->def, sizeof v->def, "%s", deffile);
    while (fgets(line, sizeof line, f) != NULL) {
        size_t n;
        char *p = line;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        n = strlen(p);
        while (n > 0u && (p[n - 1u] == '\n' || p[n - 1u] == '\r' ||
                          p[n - 1u] == ' ')) {
            p[--n] = '\0';
        }
        /* Order IS priority, which is why blank lines and comments are
         * skipped rather than counted: a comment must not silently demote
         * everything under it. */
        if (n == 0u || p[0] == '#') {
            continue;
        }
        (void)tiku_vdir_add(v, p);
    }
    (void)fclose(f);
    return v->nsrc;
}

/** @brief Join @p dir and @p name.  @return 0 on success. */
static int
join(char *out, size_t max, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    int slash = (n > 0u && dir[n - 1u] == '/') ? 0 : 1;

    if (n + (size_t)slash + strlen(name) + 1u > max) {
        return -1;
    }
    return (snprintf(out, max, "%s%s%s", dir, slash ? "/" : "", name) > 0)
               ? 0 : -1;
}

int
tiku_vdir_owner(const tiku_vdir_t *v, tiku_backend_t *b,
                    const char *name)
{
    int i;

    if (v == NULL || b == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < v->nsrc; i++) {
        char path[TIKU_PATH_MAX];
        tiku_model_t m;

        if (join(path, sizeof path, v->src[i], name) == 0 &&
            b->ops->stat(b, path, &m) == 0) {
            return i;               /* the first that has it wins */
        }
    }
    return -1;
}

int
tiku_vdir_resolve(const tiku_vdir_t *v, tiku_backend_t *b,
                      const char *name, char *out, size_t max)
{
    int i = tiku_vdir_owner(v, b, name);

    if (i < 0) {
        return -1;
    }
    return join(out, max, v->src[i], name);
}

int
tiku_vdir_after_change(const tiku_vdir_t *v, tiku_backend_t *b,
                           const char *name, int which, int created,
                           char *out, size_t max)
{
    int owner;

    if (v == NULL || b == NULL || name == NULL || out == NULL) {
        return 0;
    }
    out[0] = '\0';
    owner = tiku_vdir_owner(v, b, name);
    if (created) {
        /* A creation matters only if the new entry is the one that now
         * wins.  Appearing in a shadowed directory changes nothing that
         * can be seen, and a row for it would be a row for a file the
         * user cannot reach by that name (PVN-061). */
        if (owner != which) {
            return 0;
        }
        return (join(out, max, v->src[which], name) == 0);
    }
    if (owner == which) {
        return 0;                   /* still present: nothing was removed */
    }
    if (owner < 0) {
        return 1;                   /* the last copy went: the row goes */
    }
    /* Whether this removal is visible is a question about PRIORITY, and
     * the entry is already gone -- so the only evidence left is where the
     * removal happened relative to whoever owns the name now.  A removal
     * BELOW the current owner was shadowed and changes nothing; one above
     * it was the winner, and what it was hiding is revealed (PVN-063). */
    if (which > owner) {
        return 0;
    }
    return (join(out, max, v->src[owner], name) == 0);
}

/** @brief Collector for the merged listing. */
struct merge_ctx {
    tiku_model_t *out;
    int               n, max;
};

static int
merge_one(const tiku_model_t *m, void *ctx)
{
    struct merge_ctx *c = ctx;
    int j;

    if (c->n >= c->max) {
        return 1;                   /* full: stop the walk */
    }
    /* Already supplied by a higher-priority directory: shadowed, so it
     * must not appear twice (PVN-061). */
    for (j = 0; j < c->n; j++) {
        if (strcmp(c->out[j].name, m->name) == 0) {
            return 0;
        }
    }
    c->out[c->n++] = *m;
    return 0;
}

int
tiku_vdir_list(const tiku_vdir_t *v, tiku_backend_t *b,
                   tiku_model_t *out, int max)
{
    struct merge_ctx c;
    int i;

    if (v == NULL || b == NULL || out == NULL) {
        return -1;
    }
    c.out = out;
    c.n = 0;
    c.max = max;
    /* In priority order, so the first contributor to offer a name is the
     * one whose entry survives the duplicate test above. */
    for (i = 0; i < v->nsrc; i++) {
        (void)b->ops->list(b, v->src[i], merge_one, &c);
    }
    return c.n;
}

void
tiku_vdir_child(const tiku_vdir_t *v, tiku_backend_t *b,
                    const char *name, tiku_vdir_t *out)
{
    int i;

    if (v == NULL || b == NULL || name == NULL || out == NULL) {
        return;
    }
    tiku_vdir_init(out);
    /* Every contributor that HAS the subdirectory contributes it, in the
     * same order: a subdirectory of a merged directory is itself merged,
     * or descending into one would quietly leave the merge behind. */
    for (i = 0; i < v->nsrc; i++) {
        char path[TIKU_PATH_MAX];
        tiku_model_t m;

        if (join(path, sizeof path, v->src[i], name) == 0 &&
            b->ops->stat(b, path, &m) == 0 &&
            tiku_model_is_container(&m)) {
            (void)tiku_vdir_add(out, path);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Watching                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief A path's modification time, or -1 when it is not there. */
static int64_t
mtime_of(const char *path)
{
    struct stat st;

    if (path == NULL || path[0] == '\0' || stat(path, &st) != 0) {
        return -1;
    }
    return (int64_t)st.st_mtime;
}

void
tiku_vdir_watch_init(tiku_vdir_watch_t *w, const tiku_vdir_t *v)
{
    int i;

    if (w == NULL || v == NULL) {
        return;
    }
    memset(w, 0, sizeof *w);
    w->def_mtime = mtime_of(v->def);
    w->nsrc = v->nsrc;
    for (i = 0; i < v->nsrc; i++) {
        w->src_mtime[i] = mtime_of(v->src[i]);
        w->src_present[i] = (w->src_mtime[i] >= 0);
    }
    w->primed = 1;
}

unsigned
tiku_vdir_poll(const tiku_vdir_t *v, tiku_vdir_watch_t *w,
                   int *gone)
{
    unsigned ch = TIKU_VDIR_CH_NONE;
    int i;

    if (gone != NULL) {
        *gone = -1;
    }
    if (v == NULL || w == NULL) {
        return ch;
    }
    if (!w->primed) {
        /* An unprimed watch reports nothing: the first poll establishes
         * what "unchanged" means, or every directory would appear to have
         * just changed the moment it was opened. */
        tiku_vdir_watch_init(w, v);
        return ch;
    }
    if (v->def[0] != '\0') {
        int64_t t = mtime_of(v->def);

        if (t != w->def_mtime) {
            w->def_mtime = t;
            ch |= TIKU_VDIR_CH_DEF;
        }
    }
    for (i = 0; i < v->nsrc && i < TIKU_VDIR_MAX_SRC; i++) {
        int64_t t = mtime_of(v->src[i]);
        int here = (t >= 0);

        if (w->src_present[i] && !here) {
            /* A contributor that has gone takes its rows with it, and the
             * caller has to be told WHICH so it can find them (PVN-064). */
            ch |= TIKU_VDIR_CH_GONE;
            if (gone != NULL && *gone < 0) {
                *gone = i;
            }
        } else if (here && t != w->src_mtime[i]) {
            ch |= TIKU_VDIR_CH_SRC;
        }
        w->src_mtime[i] = t;
        w->src_present[i] = here;
    }
    return ch;
}

int
tiku_vdir_supplied_by(const tiku_vdir_t *v, tiku_backend_t *b,
                          const char *name, int which)
{
    return (tiku_vdir_owner(v, b, name) == which);
}

void
tiku_vdir_drop(tiku_vdir_t *v, int which)
{
    int i;

    if (v == NULL || which < 0 || which >= v->nsrc) {
        return;
    }
    for (i = which + 1; i < v->nsrc; i++) {
        memcpy(v->src[i - 1], v->src[i], sizeof v->src[0]);
    }
    v->nsrc--;
}

int
tiku_vdir_cleanup(tiku_vdir_t *v, const char *scratch_dir)
{
    int removed = 0;

    if (v == NULL) {
        return -1;
    }
    if (v->def[0] != '\0' && unlink(v->def) == 0) {
        removed = 1;
    }
    if (scratch_dir != NULL && scratch_dir[0] != '\0' &&
        rmdir(scratch_dir) == 0) {
        removed = 1;
    }
    v->def[0] = '\0';
    return removed;
}

uint64_t
tiku_vdir_child_id(const tiku_vdir_t *v, const char *name)
{
    uint64_t h = 1469598103934665603ull;    /* FNV-1a */
    const unsigned char *p;
    int i;

    if (v == NULL || name == NULL) {
        return 0;
    }
    /* Over the CONTRIBUTORS and the name, not over the name alone: two
     * different merges that happen to hold a folder of the same name are
     * different folders, and a window keyed on one must not answer for the
     * other. */
    for (i = 0; i < v->nsrc; i++) {
        for (p = (const unsigned char *)v->src[i]; *p != '\0'; p++) {
            h = (h ^ *p) * 1099511628211ull;
        }
        h = (h ^ 0x2fu) * 1099511628211ull;
    }
    for (p = (const unsigned char *)name; *p != '\0'; p++) {
        h = (h ^ *p) * 1099511628211ull;
    }
    return h;
}
