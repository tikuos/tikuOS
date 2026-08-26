/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_list.c - the rows, the selection, the anchors and the rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "tiku_list.h"
#include "tiku_font.h"
#include "tiku_ui.h"

static int
clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/**
 * @brief The rule everything else leans on.
 *
 * An empty selection has no anchors; a selection that is not empty has
 * a valid one.  Called at the end of every gesture rather than trusted
 * to fall out of it -- which is how an anchor came to point at whatever
 * slid into a removed row's place in the view this was lifted from.
 */
static void
repair(tiku_list_t *l, int because_of)
{
    if (tiku_list_selection_count(l) == 0) {
        l->pivot = -1;
        l->last_added = -1;
        return;
    }
    if (l->pivot < 0 || l->pivot >= l->count || !l->sel[l->pivot]) {
        l->pivot = (because_of >= 0 && because_of < l->count &&
                    l->sel[because_of])
                       ? because_of
                       : tiku_list_chosen(l);
    }
    if (l->last_added < 0 || l->last_added >= l->count) {
        l->last_added = l->pivot;
    }
}

void
tiku_list_init(tiku_list_t *l, int single)
{
    if (l == NULL) {
        return;
    }
    memset(l, 0, sizeof *l);
    l->cursor = -1;
    l->pivot = -1;
    l->last_added = -1;
    l->single = single ? 1 : 0;
}

int
tiku_list_set_count(tiku_list_t *l, int count)
{
    int i;

    if (l == NULL) {
        return 0;
    }
    if (count < 0) {
        count = 0;
    }
    if (count > TIKU_LIST_MAX) {
        count = TIKU_LIST_MAX;
    }
    /* Rows that no longer exist stop being selected, so a list that
     * shrinks under a selection cannot leave a flag set where a later
     * row will land. */
    for (i = count; i < l->count && i < TIKU_LIST_MAX; i++) {
        l->sel[i] = 0u;
        l->head[i] = 0u;
    }
    l->count = count;
    if (l->cursor >= count) {
        l->cursor = count - 1;
    }
    if (l->top > count - 1) {
        l->top = (count > 0) ? count - 1 : 0;
    }
    if (l->top < 0) {
        l->top = 0;
    }
    repair(l, -1);
    return count;
}

int
tiku_list_count(const tiku_list_t *l)
{
    return (l != NULL) ? l->count : 0;
}

int
tiku_list_row_h(const tiku_list_t *l)
{
    if (l != NULL && l->row_h > 0) {
        return l->row_h;
    }
    return tiku_ui_row_h();
}

void
tiku_list_set_row_h(tiku_list_t *l, int h)
{
    if (l != NULL) {
        l->row_h = (h > 0) ? h : 0;
    }
}

int
tiku_list_visible_rows(const tiku_list_t *l, tiku_rect_t body)
{
    int h = tiku_list_row_h(l);

    if (h <= 0 || body.h <= 0) {
        return 0;
    }
    return body.h / h;
}

tiku_rect_t
tiku_list_row_rect(const tiku_list_t *l, tiku_rect_t body, int row)
{
    tiku_rect_t r = body;
    int h = tiku_list_row_h(l);

    if (l == NULL || row < 0 || row >= l->count) {
        r.h = 0;
        return r;
    }
    r.y = body.y + (row - l->top) * h;
    r.h = h;
    return r;
}

int
tiku_list_at(const tiku_list_t *l, tiku_rect_t body, int x, int y)
{
    int h = tiku_list_row_h(l);
    int row;

    if (l == NULL || h <= 0 || x < body.x || x >= body.x + body.w ||
        y < body.y || y >= body.y + body.h) {
        return -1;
    }
    row = l->top + (y - body.y) / h;
    return (row >= 0 && row < l->count) ? row : -1;
}

void
tiku_list_set_heading(tiku_list_t *l, int row, int heading)
{
    if (l == NULL || row < 0 || row >= l->count) {
        return;
    }
    l->head[row] = (unsigned char)(heading ? 1 : 0);
    if (l->head[row]) {
        /* A row that BECOMES furniture stops being picked, or the
         * selection would hold something no gesture could let go of. */
        l->sel[row] = 0u;
        if (l->cursor == row) {
            l->cursor = -1;
        }
        repair(l, -1);
    }
}

int
tiku_list_is_heading(const tiku_list_t *l, int row)
{
    return (l != NULL && row >= 0 && row < l->count && l->head[row]);
}

int
tiku_list_selected(const tiku_list_t *l, int row)
{
    if (l == NULL || row < 0 || row >= l->count) {
        return 0;
    }
    return l->sel[row] ? 1 : 0;
}

int
tiku_list_selection_count(const tiku_list_t *l)
{
    int i, n = 0;

    if (l == NULL) {
        return 0;
    }
    for (i = 0; i < l->count; i++) {
        n += l->sel[i] ? 1 : 0;
    }
    return n;
}

int
tiku_list_chosen(const tiku_list_t *l)
{
    int i;

    if (l == NULL) {
        return -1;
    }
    for (i = 0; i < l->count; i++) {
        if (l->sel[i]) {
            return i;
        }
    }
    return -1;
}

void
tiku_list_select_only(tiku_list_t *l, int row)
{
    if (l == NULL || row < 0 || row >= l->count ||
        tiku_list_is_heading(l, row)) {
        return;
    }
    memset(l->sel, 0, (size_t)l->count);
    l->sel[row] = 1u;
    l->cursor = row;
    l->pivot = row;
    l->last_added = row;
}

void
tiku_list_select_all(tiku_list_t *l)
{
    if (l == NULL || l->count == 0) {
        return;
    }
    if (l->single) {
        /* Holding one row at a time, "all" is the first of them -- the
         * rule is kept here rather than asked of every caller. */
        tiku_list_select_only(l, 0);
        return;
    }
    {
        int i;

        for (i = 0; i < l->count; i++) {
            l->sel[i] = (unsigned char)!l->head[i];
        }
    }
    /* Both anchors are re-taken, not merely filled in when missing: one
     * left over from an earlier click would make the next shift-click
     * reach from a row that has nothing to do with this selection. */
    l->pivot = 0;
    l->last_added = l->count - 1;
    repair(l, -1);
}

void
tiku_list_select_none(tiku_list_t *l)
{
    if (l == NULL) {
        return;
    }
    memset(l->sel, 0, sizeof l->sel);
    l->pivot = -1;
    l->last_added = -1;
}

void
tiku_list_invert(tiku_list_t *l)
{
    int i;

    if (l == NULL || l->count == 0) {
        return;
    }
    if (l->single) {
        return;             /* one at a time: there is nothing to flip to */
    }
    l->pivot = -1;
    l->last_added = -1;
    for (i = 0; i < l->count; i++) {
        l->sel[i] = (unsigned char)(!l->sel[i] && !l->head[i]);
    }
    if (l->sel[0]) {
        l->pivot = 0;
        l->last_added = l->count - 1;
    }
}

/** @brief Give rows @p a..@p b (either order) the state @p on. */
static void
range(tiku_list_t *l, int a, int b, int on)
{
    int lo, hi, i;

    if (a < 0 || b < 0 || a >= l->count || b >= l->count) {
        return;
    }
    if (l->single) {
        /* A range is an extension, and extending is what single mode
         * refuses -- so it lands on the row the gesture reached. */
        tiku_list_select_only(l, b);
        return;
    }
    lo = (a < b) ? a : b;
    hi = (a < b) ? b : a;
    for (i = lo; i <= hi; i++) {
        if (l->head[i]) {
            continue;       /* reached OVER, not taken */
        }
        l->sel[i] = (unsigned char)(on ? 1 : 0);
    }
    l->last_added = b;
}

void
tiku_list_click(tiku_list_t *l, int row, unsigned modifiers)
{
    int toggle, extend;

    if (l == NULL) {
        return;
    }
    if (row < 0 || row >= l->count) {
        /* The empty space below the rows is a place too, and clicking
         * it means "none of them". */
        tiku_list_select_none(l);
        l->cursor = -1;
        return;
    }
    if (tiku_list_is_heading(l, row)) {
        /* Furniture, not a row: pressing the word that NAMES a group
         * neither picks anything nor lets go of what is picked. */
        return;
    }
    toggle = (modifiers & TIKU_MOD_CMD) != 0u;
    extend = (modifiers & TIKU_MOD_SHIFT) != 0u;
    l->cursor = row;

    if (extend && toggle) {
        /* The range takes what the clicked row did NOT have, so
         * reaching out to ADD to a selection cannot clear it. */
        int on = !l->sel[row];

        if (l->pivot >= 0) {
            range(l, l->pivot, row, on);
        } else {
            l->sel[row] = (unsigned char)(on ? 1 : 0);
            l->last_added = row;
        }
    } else if (extend) {
        int from = (l->pivot >= 0) ? l->pivot : row;

        if (!l->single) {
            memset(l->sel, 0, (size_t)l->count);
        }
        range(l, from, row, 1);
        /* The pivot is NOT moved: a second shift-click reaches from the
         * same place rather than growing what the last one left. */
        if (l->pivot < 0) {
            l->pivot = row;
        }
    } else if (toggle) {
        if (l->single) {
            tiku_list_select_only(l, row);
        } else {
            l->sel[row] = (unsigned char)!l->sel[row];
            if (l->sel[row]) {
                l->pivot = row;
                l->last_added = row;
            }
        }
    } else if (l->sel[row] &&
               (tiku_list_selection_count(l) > 1 || row == l->pivot)) {
        /* Already part of the selection: nothing changes, which is what
         * lets a gesture that starts on a selected row carry the whole
         * selection with it. */
        return;
    } else {
        tiku_list_select_only(l, row);
    }
    repair(l, row);
}

/**
 * @brief The first row at or past @p from that can be landed on.
 *
 * Travel goes ON in the direction it was already going, and only then
 * turns back: HOME onto a heading means the first row UNDER it, not the
 * nothing that lies before it.
 *
 * @return that row, or -1 when the list is all furniture.
 */
static int
landable(const tiku_list_t *l, int from, int dir)
{
    int i;

    for (i = from; i >= 0 && i < l->count; i += dir) {
        if (!l->head[i]) {
            return i;
        }
    }
    for (i = from; i >= 0 && i < l->count; i -= dir) {
        if (!l->head[i]) {
            return i;
        }
    }
    return -1;
}

/** @brief Move the cursor to @p to, taking the selection with it. */
static void
go(tiku_list_t *l, int to, unsigned modifiers, tiku_rect_t body)
{
    int dir;

    if (l->count == 0) {
        return;
    }
    dir = (to >= l->cursor) ? 1 : -1;
    to = clampi(to, 0, l->count - 1);
    to = landable(l, to, dir);
    if (to < 0) {
        return;             /* nothing here a cursor could stand on */
    }
    l->cursor = to;
    if ((modifiers & TIKU_MOD_SHIFT) != 0u && !l->single) {
        int from = (l->pivot >= 0) ? l->pivot : to;

        memset(l->sel, 0, (size_t)l->count);
        range(l, from, to, 1);
        if (l->pivot < 0) {
            l->pivot = to;
        }
    } else {
        tiku_list_select_only(l, to);
    }
    repair(l, to);
    (void)tiku_list_reveal(l, body);
}

int
tiku_list_key(tiku_list_t *l, unsigned key, unsigned modifiers,
              tiku_rect_t body)
{
    int page;

    if (l == NULL || l->count == 0) {
        return 0;
    }
    page = tiku_list_visible_rows(l, body);
    if (page < 1) {
        page = 1;
    }
    /*
     * A cursor that has not been placed starts where the selection is.
     * With no selection either, the first arrow LANDS on the row the
     * list starts from rather than moving off it -- moving from nowhere
     * has to begin somewhere, and beginning one row in is how a list
     * silently skips its first entry.
     */
    if (l->cursor < 0) {
        int chosen = tiku_list_chosen(l);

        if (chosen >= 0) {
            l->cursor = chosen;
        } else {
            l->cursor = l->top;
            if (key == TIKU_KEY_UP || key == TIKU_KEY_DOWN ||
                key == TIKU_KEY_PAGE_UP || key == TIKU_KEY_PAGE_DOWN) {
                go(l, l->top, modifiers, body);
                return 1;
            }
        }
    }
    switch (key) {
    case TIKU_KEY_UP:
        go(l, l->cursor - 1, modifiers, body);
        return 1;
    case TIKU_KEY_DOWN:
        go(l, l->cursor + 1, modifiers, body);
        return 1;
    case TIKU_KEY_PAGE_UP:
        go(l, l->cursor - page, modifiers, body);
        return 1;
    case TIKU_KEY_PAGE_DOWN:
        go(l, l->cursor + page, modifiers, body);
        return 1;
    case TIKU_KEY_HOME:
        go(l, 0, modifiers, body);
        return 1;
    case TIKU_KEY_END:
        go(l, l->count - 1, modifiers, body);
        return 1;
    default:
        break;
    }
    if ((key == 'a' || key == 'A') && (modifiers & TIKU_MOD_CMD) != 0u) {
        tiku_list_select_all(l);
        return 1;
    }
    return 0;
}

static int
lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

int
tiku_list_typeahead(tiku_list_t *l, char ch, int64_t now,
                    tiku_rect_t body, tiku_list_name_fn name, void *ctx)
{
    size_t used;
    int i;

    if (l == NULL || name == NULL || l->count == 0 || ch < ' ') {
        return -1;
    }
    if (now - l->typed_at > TIKU_LIST_TYPEAHEAD_US) {
        l->typed[0] = '\0';     /* the last attempt has lapsed */
    }
    l->typed_at = now;
    used = strlen(l->typed);
    if (used + 1u < sizeof l->typed) {
        l->typed[used] = ch;
        l->typed[used + 1u] = '\0';
        used++;
    }
    for (i = 0; i < l->count; i++) {
        const char *n = name(ctx, i);
        size_t k;

        if (n == NULL || l->head[i]) {
            continue;       /* furniture is not what a name spells to */
        }
        for (k = 0; k < used; k++) {
            if (n[k] == '\0' ||
                lower((unsigned char)n[k]) != lower((unsigned char)l->typed[k])) {
                break;
            }
        }
        if (k == used) {
            tiku_list_select_only(l, i);
            (void)tiku_list_reveal(l, body);
            return i;
        }
    }
    return -1;                  /* still being typed: nothing moves */
}

int
tiku_list_reveal(tiku_list_t *l, tiku_rect_t body)
{
    int rows = tiku_list_visible_rows(l, body);
    int was;

    if (l == NULL || l->cursor < 0 || rows < 1) {
        return 0;
    }
    was = l->top;
    if (l->cursor < l->top) {
        l->top = l->cursor;
    } else if (l->cursor >= l->top + rows) {
        l->top = l->cursor - rows + 1;
    }
    if (l->top < 0) {
        l->top = 0;
    }
    return l->top != was;
}

int
tiku_list_scroll_to(tiku_list_t *l, int top, tiku_rect_t body)
{
    int rows = tiku_list_visible_rows(l, body);
    int most, was;

    if (l == NULL) {
        return 0;
    }
    was = l->top;
    most = l->count - rows;
    if (most < 0) {
        most = 0;               /* it all fits: there is nothing to scroll */
    }
    l->top = clampi(top, 0, most);
    return l->top != was;
}

void
tiku_list_draw(const tiku_list_t *l, tiku_surface_t *s, tiku_rect_t body,
               tiku_list_name_fn name, void *ctx)
{
    int rows, i;

    if (l == NULL || s == NULL || name == NULL) {
        return;
    }
    rows = tiku_list_visible_rows(l, body);
    for (i = 0; i < rows; i++) {
        int row = l->top + i;
        const char *n;

        if (row >= l->count) {
            break;
        }
        n = name(ctx, row);
        if (l->head[row]) {
            /* Words, not a row: the group's NAME, in the face labels
             * wear, over the paper the rows stand on. */
            tiku_rect_t r = tiku_list_row_rect(l, body, row);
            const tiku_font_t *f = tiku_font_bold();

            tiku_fill(s, r, TIKU_C_DOC);
            tiku_text(s, f, r.x + 4,
                           r.y + (r.h - f->height) / 2 + f->ascent,
                           (n != NULL) ? n : "", TIKU_C_TEXT);
            continue;
        }
        tiku_ui_list_row(s, tiku_list_row_rect(l, body, row),
                         (n != NULL) ? n : "", tiku_list_selected(l, row));
    }
}
