/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tabs.c - the tab strip's state, layout, hit-test and look.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_tabs.h"
#include "tiku_font.h"

void
tiku_tabs_init(tiku_tabs_t *t)
{
    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof *t);
    t->current = -1;
}

int
tiku_tabs_add(tiku_tabs_t *t, const char *label)
{
    int at;

    if (t == NULL || label == NULL || t->count >= TIKU_TABS_MAX) {
        return -1;
    }
    at = t->count;
    snprintf(t->label[at], sizeof t->label[at], "%s", label);
    t->count++;
    if (t->current < 0) {
        t->current = 0;             /* the first tab is current */
    }
    return at;
}

int
tiku_tabs_count(const tiku_tabs_t *t)
{
    return (t != NULL) ? t->count : 0;
}

int
tiku_tabs_current(const tiku_tabs_t *t)
{
    return (t != NULL) ? t->current : -1;
}

int
tiku_tabs_select(tiku_tabs_t *t, int index)
{
    if (t == NULL || t->count == 0) {
        return -1;
    }
    if (index < 0) {
        index = 0;
    } else if (index >= t->count) {
        index = t->count - 1;
    }
    t->current = index;
    return t->current;
}

int
tiku_tabs_next(tiku_tabs_t *t, int dir)
{
    if (t == NULL || t->count == 0) {
        return -1;
    }
    /* Wrap both ways: the modulo is written to stay non-negative for a
     * dir of -1, where a plain % would land on a negative index. */
    t->current = ((t->current + dir) % t->count + t->count) % t->count;
    return t->current;
}

tiku_rect_t
tiku_tabs_rect(const tiku_tabs_t *t, tiku_rect_t strip, int index)
{
    tiku_rect_t r = strip;
    int n = (t != NULL) ? t->count : 0;

    if (n <= 0 || index < 0 || index >= n) {
        r.w = 0;
        return r;
    }
    /*
     * Even division, and the LAST tab takes whatever the division could
     * not share out, so the tabs together cover the strip exactly rather
     * than leaving a sliver of body-coloured gap at the right edge that a
     * click could fall into.
     */
    r.w = strip.w / n;
    r.x = strip.x + index * r.w;
    if (index == n - 1) {
        r.w = strip.x + strip.w - r.x;
    }
    return r;
}

int
tiku_tabs_hit(const tiku_tabs_t *t, tiku_rect_t strip, int x, int y)
{
    int i, n = (t != NULL) ? t->count : 0;

    for (i = 0; i < n; i++) {
        tiku_rect_t r = tiku_tabs_rect(t, strip, i);

        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return i;
        }
    }
    return -1;
}

void
tiku_tabs_draw(const tiku_tabs_t *t, tiku_surface_t *s, tiku_rect_t strip)
{
    const tiku_font_t *font = tiku_font_plain();
    int n = (t != NULL) ? t->count : 0;
    int seam = strip.y + strip.h - 1;
    int i;

    if (s == NULL || n <= 0) {
        return;
    }
    /* The line the body's top edge sits on, under the whole strip.  The
     * current tab erases its own stretch of it, which is what "open to
     * the body" is drawn out of. */
    tiku_hline(s, strip.x, seam, strip.w, tiku_tint(TIKU_C_PANEL, 0.70f));

    for (i = 0; i < n; i++) {
        tiku_rect_t r = tiku_tabs_rect(t, strip, i);
        int active = (i == t->current);
        tiku_rgb_t face = active ? TIKU_C_TAB : TIKU_C_TAB_IDLE;
        int tw = tiku_text_width(font, t->label[i]);
        int tx = r.x + (r.w - tw) / 2;
        int ty = r.y + (r.h - font->height) / 2 + font->ascent;

        /* An idle tab sits a shade lower and keeps the seam under it; the
         * current one fills to the seam and rubs it out, joining the body.
         */
        if (active) {
            tiku_fill(s, (tiku_rect_t){ r.x, r.y, r.w, r.h }, face);
            tiku_hline(s, r.x, seam, r.w, face);
        } else {
            tiku_fill(s, (tiku_rect_t){ r.x, r.y + 2, r.w, r.h - 3 }, face);
        }
        /* A hairline down the right of every tab but the last, so the row
         * reads as separate tabs and not one long fill. */
        if (i < n - 1) {
            tiku_vline(s, r.x + r.w - 1, r.y + 2, r.h - 3,
                       tiku_tint(TIKU_C_PANEL, 0.70f));
        }
        tiku_text(s, font, tx, ty, t->label[i], TIKU_C_TABTEXT);
    }
}
