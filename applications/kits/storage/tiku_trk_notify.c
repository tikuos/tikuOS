/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_notify.c - deriving change notices, and parking them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_notify.h"

#include <stdio.h>
#include <string.h>

void
tiku_trk_evlist_clear(tiku_trk_evlist_t *l)
{
    if (l != NULL) {
        l->count = 0;
        l->overflowed = 0;
    }
}

/** @brief The directory part of @p path, into @p out. */
static void
parent_of(const char *path, char *out, size_t max)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL) {
        snprintf(out, max, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n == 0u) {
        snprintf(out, max, "/");
        return;
    }
    if (n >= max) {
        n = max - 1u;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

int
tiku_trk_evlist_add(tiku_trk_evlist_t *l, tiku_trk_evop_t op,
                    const tiku_trk_model_t *m, const char *from, int64_t at)
{
    tiku_trk_event_t *e;

    if (l == NULL || m == NULL) {
        return 0;
    }
    if (l->count >= TIKU_TRK_EVENTS_MAX) {
        /* Saying so is the point: a caller that silently lost notices would
         * leave the view believing it is current. */
        l->overflowed = 1;
        return 0;
    }
    e = &l->ev[l->count++];
    memset(e, 0, sizeof *e);
    e->op = op;
    e->model = *m;
    e->at = at;
    snprintf(e->path, sizeof e->path, "%s", m->path);
    if (from != NULL) {
        snprintf(e->from, sizeof e->from, "%s", from);
    }
    parent_of(e->path, e->dir, sizeof e->dir);
    return 1;
}

int
tiku_trk_evlist_add_fields(tiku_trk_evlist_t *l, tiku_trk_evop_t op,
                           const tiku_trk_model_t *m, const char *from,
                           int64_t at, unsigned fields)
{
    if (!tiku_trk_evlist_add(l, op, m, from, at)) {
        return 0;
    }
    l->ev[l->count - 1].fields = fields;
    return 1;
}

/** @brief WHICH displayed facts moved, as TIKU_TRK_EVF_* bits (MA-072). */
static unsigned
facts_fields(const tiku_trk_model_t *a, const tiku_trk_model_t *b)
{
    unsigned f = 0u;

    if (a->facts.size != b->facts.size) { f |= TIKU_TRK_EVF_SIZE; }
    if (a->facts.mtime != b->facts.mtime) { f |= TIKU_TRK_EVF_DATE; }
    if (a->facts.perm != b->facts.perm) { f |= TIKU_TRK_EVF_PERM; }
    /* A link losing its target changes nothing else about the entry,
     * so without this term the diff cannot see it happen and the row
     * goes on claiming the link is live.  A Trash filling or emptying is
     * the same shape (AW-029).  All of these bear on the ICON. */
    if (a->kind != b->kind ||
        a->facts.link_broken != b->facts.link_broken ||
        a->facts.trash_full != b->facts.trash_full ||
        strcmp(a->type, b->type) != 0) {
        f |= TIKU_TRK_EVF_KIND;
    }
    /* A volume's free space moves without anything else moving, which is
     * the whole of what a space bar shows (AW-097) -- a visual fact like
     * the icon, and the total is also the Size a volume's row reports. */
    if (a->facts.total != b->facts.total ||
        a->facts.avail != b->facts.avail) {
        f |= TIKU_TRK_EVF_KIND | TIKU_TRK_EVF_SIZE;
    }
    if (strcmp(a->facts.meta, b->facts.meta) != 0) {
        f |= TIKU_TRK_EVF_VALUE;
    }
    return f;
}

/** @brief Whether the facts a row displays have moved. */
static int
facts_differ(const tiku_trk_model_t *a, const tiku_trk_model_t *b)
{
    return facts_fields(a, b) != 0u;
}

/** @brief Index of @p path in a listing, or -1. */
static int
find_path(const tiku_trk_model_t *set, int n, const char *path)
{
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(set[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_notify_diff(const tiku_trk_model_t *was, int nwas,
                     const tiku_trk_model_t *now, int nnow, int64_t at,
                     tiku_trk_evlist_t *out)
{
    unsigned char paired[256];
    int i, j, before;

    if (out == NULL) {
        return 0;
    }
    before = out->count;
    memset(paired, 0, sizeof paired);

    /* A removal and a creation sharing a node id in one pass are a MOVE.
     * Reported as two events the row would be destroyed and rebuilt, losing
     * its selection, its place in the list and its saved position -- which
     * is precisely what a rename must not do. */
    for (i = 0; i < nwas; i++) {
        if (find_path(now, nnow, was[i].path) >= 0) {
            continue;                      /* still there                */
        }
        for (j = 0; j < nnow && j < (int)sizeof paired; j++) {
            if (paired[j] || was[i].node_id == 0 ||
                now[j].node_id != was[i].node_id) {
                continue;
            }
            if (find_path(was, nwas, now[j].path) >= 0) {
                continue;                  /* that one is not new         */
            }
            paired[j] = 1;
            (void)tiku_trk_evlist_add(out, TIKU_TRK_EV_MOVED, &now[j],
                                      was[i].path, at);
            break;
        }
        if (j >= nnow || j >= (int)sizeof paired) {
            (void)tiku_trk_evlist_add(out, TIKU_TRK_EV_REMOVED, &was[i],
                                      NULL, at);
        }
    }
    for (j = 0; j < nnow; j++) {
        int k;

        if (j < (int)sizeof paired && paired[j]) {
            continue;
        }
        k = find_path(was, nwas, now[j].path);
        if (k < 0) {
            (void)tiku_trk_evlist_add(out, TIKU_TRK_EV_CREATED, &now[j],
                                      NULL, at);
        } else if (facts_differ(&was[k], &now[j])) {
            (void)tiku_trk_evlist_add_fields(out, TIKU_TRK_EV_CHANGED,
                                             &now[j], NULL, at,
                                             facts_fields(&was[k], &now[j]));
        }
    }
    return out->count - before;
}

int
tiku_trk_evlist_coalesce(const tiku_trk_evlist_t *in,
                         tiku_trk_evlist_t *out)
{
    int i, j;

    if (in == NULL || out == NULL || in == out) {
        return 0;
    }
    tiku_trk_evlist_clear(out);
    for (i = 0; i < in->count; i++) {
        const tiku_trk_event_t *src = &in->ev[i];
        tiku_trk_event_t *dst = NULL;

        for (j = 0; j < out->count; j++) {
            if (strcmp(out->ev[j].path, src->path) == 0) {
                dst = &out->ev[j];
                break;
            }
        }
        if (dst == NULL) {
            if (out->count >= TIKU_TRK_EVENTS_MAX) {
                out->overflowed = 1;
                continue;
            }
            out->ev[out->count++] = *src;
            continue;
        }
        /* Preserve the first lifecycle edge.  A row created during a burst
         * must still be inserted, while later changes only refresh its
         * payload.  A move retains the original source path for the same
         * reason. */
        if (dst->op == TIKU_TRK_EV_CREATED) {
            dst->model = src->model;
            dst->at = src->at;
            continue;
        }
        if (dst->op == TIKU_TRK_EV_MOVED && src->op != TIKU_TRK_EV_REMOVED) {
            dst->model = src->model;
            dst->at = src->at;
            continue;
        }
        if (src->op == TIKU_TRK_EV_REMOVED ||
            src->op == TIKU_TRK_EV_MOVED ||
            src->op == TIKU_TRK_EV_CHANGED ||
            src->op == TIKU_TRK_EV_ATTR) {
            dst->op = src->op;
            dst->model = src->model;
            dst->at = src->at;
            if (src->from[0] != '\0' && dst->from[0] == '\0') {
                snprintf(dst->from, sizeof dst->from, "%s", src->from);
            }
            snprintf(dst->dir, sizeof dst->dir, "%s", src->dir);
        }
    }
    out->overflowed = in->overflowed || out->overflowed;
    return out->count;
}

/*---------------------------------------------------------------------------*/
/* Parking                                                                   */
/*---------------------------------------------------------------------------*/

void
tiku_trk_park_clear(tiku_trk_park_t *p)
{
    if (p != NULL) {
        p->count = 0;
    }
}

int
tiku_trk_park_add(tiku_trk_park_t *p, const tiku_trk_event_t *e)
{
    if (p == NULL || e == NULL) {
        return 0;
    }
    if (p->count >= (int)(sizeof p->ev / sizeof p->ev[0])) {
        /* Full.  The OLDEST notice gives way, as the device's own ring does:
         * the newest is the state the row will actually need when it
         * appears, and dropping it instead would silently replay stale
         * facts over fresh ones. */
        int i;

        for (i = 0; i < p->count - 1; i++) {
            p->ev[i] = p->ev[i + 1];
        }
        p->count--;
    }
    p->ev[p->count++] = *e;
    return 1;
}

int
tiku_trk_park_take(tiku_trk_park_t *p, const char *path,
                   tiku_trk_event_t *out, int max)
{
    int i, n = 0, w = 0;

    if (p == NULL || path == NULL) {
        return 0;
    }
    /* Walked forwards and compacted in place, so what comes out is in the
     * order it arrived: two changes to one node replayed backwards would
     * leave the earlier value showing. */
    for (i = 0; i < p->count; i++) {
        if (strcmp(p->ev[i].path, path) == 0 && n < max) {
            /* Only what is actually HANDED OVER is removed.  Taking the
             * first few and discarding the rest would lose notices the
             * caller never saw -- silently, and precisely for the node it
             * asked about. */
            if (out != NULL) {
                out[n] = p->ev[i];
            }
            n++;
            continue;
        }
        p->ev[w++] = p->ev[i];
    }
    p->count = w;
    return (n > max) ? max : n;
}

int
tiku_trk_park_expire(tiku_trk_park_t *p, int64_t now)
{
    int i, w = 0, dropped = 0;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < p->count; i++) {
        if (now - p->ev[i].at >= TIKU_TRK_PARK_LIFETIME_US) {
            dropped++;
            continue;
        }
        p->ev[w++] = p->ev[i];
    }
    p->count = w;
    return dropped;
}

int
tiku_trk_park_count(const tiku_trk_park_t *p)
{
    return (p != NULL) ? p->count : 0;
}
