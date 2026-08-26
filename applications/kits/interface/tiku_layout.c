/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_layout.c - dividing a rectangle the way it was described.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "tiku_layout.h"

void
tiku_layout_init(tiku_layout_t *l, int horiz, int gap, int pad)
{
    if (l == NULL) {
        return;
    }
    memset(l, 0, sizeof *l);
    l->horiz = horiz ? 1 : 0;
    l->gap = (gap > 0) ? gap : 0;
    l->pad = (pad > 0) ? pad : 0;
}

int
tiku_layout_add(tiku_layout_t *l, int size, int weight, int least)
{
    int at;

    if (l == NULL || l->n >= TIKU_LAYOUT_MAX) {
        return -1;
    }
    at = l->n;
    l->item[at].size = (size > 0) ? size : 0;
    /* A part with neither a size nor a weight is a spring: without this
     * an unweighted share would take nothing, and a row pushed right by
     * an empty first part would not move at all. */
    l->item[at].weight = (l->item[at].size == 0 && weight <= 0)
                             ? 1 : ((weight > 0) ? weight : 0);
    l->item[at].least = (least > 0) ? least : 0;
    l->n++;
    return at;
}

int
tiku_layout_least(const tiku_layout_t *l)
{
    int total, i;

    if (l == NULL || l->n == 0) {
        return 0;
    }
    total = 2 * l->pad + (l->n - 1) * l->gap;
    for (i = 0; i < l->n; i++) {
        int want = (l->item[i].size > 0) ? l->item[i].size
                                         : l->item[i].least;

        if (want < l->item[i].least) {
            want = l->item[i].least;
        }
        total += want;
    }
    return total;
}

/**
 * @brief Work out every part's extent along the laid axis.
 *
 * Fixed parts take their size; what is left is shared by weight, and a
 * part never goes below its least even when that means the whole
 * overruns -- a control squeezed under its own word is unreadable, and
 * a window that overruns is at least visibly wrong.  The LAST weighted
 * part takes the remainder the division could not share out, so the
 * parts together cover the run exactly rather than leaving a sliver at
 * the end that belongs to nobody.
 */
static void
solve(const tiku_layout_t *l, int extent, int *out)
{
    int shares = 0, fixed = 0, left, i, last = -1;

    for (i = 0; i < l->n; i++) {
        if (l->item[i].size > 0) {
            out[i] = (l->item[i].size > l->item[i].least)
                         ? l->item[i].size : l->item[i].least;
            fixed += out[i];
        } else {
            out[i] = 0;
            shares += l->item[i].weight;
            last = i;
        }
    }
    left = extent - 2 * l->pad - (l->n - 1) * l->gap - fixed;
    if (left < 0) {
        left = 0;
    }
    if (shares <= 0) {
        return;
    }
    for (i = 0; i < l->n; i++) {
        if (l->item[i].size > 0) {
            continue;
        }
        out[i] = left * l->item[i].weight / shares;
        if (out[i] < l->item[i].least) {
            out[i] = l->item[i].least;
        }
    }
    if (last >= 0 && out[last] >= l->item[last].least) {
        int used = 0;

        for (i = 0; i < l->n; i++) {
            if (l->item[i].size == 0 && i != last) {
                used += out[i];
            }
        }
        if (left - used >= l->item[last].least) {
            out[last] = left - used;
        }
    }
}

tiku_rect_t
tiku_layout_slot(const tiku_layout_t *l, tiku_rect_t r, int i)
{
    int extents[TIKU_LAYOUT_MAX];
    tiku_rect_t out = r;
    int at, k;

    if (l == NULL || i < 0 || i >= l->n) {
        out.w = 0;
        out.h = 0;
        return out;
    }
    solve(l, l->horiz ? r.w : r.h, extents);
    at = l->pad;
    for (k = 0; k < i; k++) {
        at += extents[k] + l->gap;
    }
    if (l->horiz) {
        out.x = r.x + at;
        out.w = extents[i];
        out.y = r.y + l->pad;
        out.h = r.h - 2 * l->pad;
        if (out.h < 0) {
            out.h = 0;
        }
    } else {
        out.y = r.y + at;
        out.h = extents[i];
        out.x = r.x + l->pad;
        out.w = r.w - 2 * l->pad;
        if (out.w < 0) {
            out.w = 0;
        }
    }
    return out;
}

int
tiku_layout_at(const tiku_layout_t *l, tiku_rect_t r, int x, int y)
{
    int i;

    if (l == NULL) {
        return -1;
    }
    for (i = 0; i < l->n; i++) {
        tiku_rect_t s = tiku_layout_slot(l, r, i);

        if (s.w > 0 && s.h > 0 && x >= s.x && x < s.x + s.w &&
            y >= s.y && y < s.y + s.h) {
            return i;
        }
    }
    return -1;
}
