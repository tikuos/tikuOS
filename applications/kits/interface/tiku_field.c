/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_field.c - the editing state behind a single-line text field.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_field.h"

#include <stdio.h>
#include <string.h>

/* Every field in the process shares one TEXT clipboard, held here.
 * Keeping the payload inside the editor also makes it impossible for a
 * focused field's Cmd-X/C/V to fall through into an application's own
 * clipboard -- a rename's Cut must never cut the selected files. */
static char edit_clipboard[TIKU_FIELD_MAX];

static void
copy_selection(tiku_field_t *e)
{
    int from, to;

    if (tiku_field_selection(e, &from, &to)) {
        size_t n = (size_t)(to - from);

        if (n >= sizeof edit_clipboard) {
            n = sizeof edit_clipboard - 1u;
        }
        memcpy(edit_clipboard, e->text + from, n);
        edit_clipboard[n] = '\0';
    } else {
        edit_clipboard[0] = '\0';
    }
}

void
tiku_field_start(tiku_field_t *e, const char *text)
{
    if (e == NULL) {
        return;
    }
    memset(e, 0, sizeof *e);
    e->deny = NULL;
    snprintf(e->text, sizeof e->text, "%s", (text != NULL) ? text : "");
    e->len = (int)strlen(e->text);
    e->caret = e->len;
    e->sel_a = 0;
    e->sel_b = e->len;
    e->active = 1;
    e->editable = 1;
}

void
tiku_field_set_editable(tiku_field_t *e, int editable)
{
    if (e != NULL) {
        e->editable = editable ? 1 : 0;
    }
}

void
tiku_field_deny(tiku_field_t *e, const char *deny)
{
    if (e != NULL) {
        e->deny = deny;
    }
}

void
tiku_field_metrics(const tiku_field_t *e, int min_w, int max_w,
                      int (*measure_prefix)(const char *, int, void *),
                      void *ctx, int *out_w, int *out_scroll)
{
    int text_w, caret_px, want, w, scroll = 0;

    if (e == NULL || measure_prefix == NULL) {
        if (out_w != NULL) { *out_w = min_w; }
        if (out_scroll != NULL) { *out_scroll = 0; }
        return;
    }
    text_w = measure_prefix(e->text, e->len, ctx);
    caret_px = measure_prefix(e->text, e->caret, ctx);
    want = text_w + 12;             /* caret room and the sunken frame */
    w = want;
    if (w < min_w) { w = min_w; }
    if (w > max_w) { w = max_w; }
    if (want > w) {
        /* The box is full: slide so the caret stays visible, but never
         * past the text's own end -- a slide that left blank space would
         * hide text for nothing. */
        if (caret_px + 8 > w - 8) {
            scroll = caret_px + 8 - (w - 8);
        }
        if (scroll > want - w) {
            scroll = want - w;
        }
        if (scroll < 0) {
            scroll = 0;
        }
    }
    if (out_w != NULL) { *out_w = w; }
    if (out_scroll != NULL) { *out_scroll = scroll; }
}

void
tiku_field_stop(tiku_field_t *e)
{
    if (e != NULL) {
        e->active = 0;
    }
}

int
tiku_field_selection(const tiku_field_t *e, int *from, int *to)
{
    int lo, hi;

    if (e == NULL || e->sel_a == e->sel_b) {
        return 0;
    }
    lo = (e->sel_a < e->sel_b) ? e->sel_a : e->sel_b;
    hi = (e->sel_a < e->sel_b) ? e->sel_b : e->sel_a;
    if (from != NULL) { *from = lo; }
    if (to != NULL)   { *to = hi; }
    return 1;
}

/** @brief Remove the selected range, leaving the caret where it was. */
static void
delete_selection(tiku_field_t *e)
{
    int lo, hi;

    if (!tiku_field_selection(e, &lo, &hi)) {
        return;
    }
    memmove(e->text + lo, e->text + hi, (size_t)(e->len - hi) + 1u);
    e->len -= (hi - lo);
    e->caret = lo;
    e->sel_a = e->sel_b = lo;
}

/** @brief Move the caret, extending or dropping the selection. */
static void
move_caret(tiku_field_t *e, int to, int shift)
{
    if (to < 0)       { to = 0; }
    if (to > e->len)  { to = e->len; }
    if (shift) {
        /* The anchor stays where the selection began, so shift-left after
         * shift-right shrinks the range rather than starting a new one. */
        if (e->sel_a == e->sel_b) {
            e->sel_a = e->caret;
        }
        e->sel_b = to;
    } else {
        e->sel_a = e->sel_b = to;
    }
    e->caret = to;
}

tiku_field_result_t
tiku_field_key(tiku_field_t *e, tiku_field_key_t key, int shift,
                  char ch)
{
    if (e == NULL || !e->active) {
        return TIKU_FIELD_IGNORED;
    }
    switch (key) {
    case TIKU_FIELD_CHAR:
        if (!e->editable) {
            return TIKU_FIELD_IGNORED;
        }
        if (ch < 0x20 || ch == 0x7f) {
            return TIKU_FIELD_IGNORED;
        }
        if (e->deny != NULL && strchr(e->deny, ch) != NULL) {
            /* Swallowed as it is typed, not refused at commit: a character
             * that cannot be in the name should never appear in the field,
             * or the user finishes typing and is then told to start again. */
            return TIKU_FIELD_IGNORED;
        }
        delete_selection(e);
        if (e->len + 1 >= (int)sizeof e->text) {
            return TIKU_FIELD_HANDLED;
        }
        memmove(e->text + e->caret + 1, e->text + e->caret,
                (size_t)(e->len - e->caret) + 1u);
        e->text[e->caret] = ch;
        e->len++;
        e->caret++;
        e->sel_a = e->sel_b = e->caret;
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_LEFT:
        move_caret(e, e->caret - 1, shift);
        return TIKU_FIELD_HANDLED;
    case TIKU_FIELD_RIGHT:
        move_caret(e, e->caret + 1, shift);
        return TIKU_FIELD_HANDLED;
    case TIKU_FIELD_HOME:
        move_caret(e, 0, shift);
        return TIKU_FIELD_HANDLED;
    case TIKU_FIELD_END:
        move_caret(e, e->len, shift);
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_BACKSPACE:
        if (!e->editable) {
            return TIKU_FIELD_HANDLED;
        }
        if (tiku_field_selection(e, NULL, NULL)) {
            delete_selection(e);
        } else if (e->caret > 0) {
            memmove(e->text + e->caret - 1, e->text + e->caret,
                    (size_t)(e->len - e->caret) + 1u);
            e->len--;
            e->caret--;
            e->sel_a = e->sel_b = e->caret;
        }
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_DELETE:
        if (!e->editable) {
            return TIKU_FIELD_HANDLED;
        }
        if (tiku_field_selection(e, NULL, NULL)) {
            delete_selection(e);
        } else if (e->caret < e->len) {
            memmove(e->text + e->caret, e->text + e->caret + 1,
                    (size_t)(e->len - e->caret));
            e->len--;
        }
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_SELECT_ALL:
        e->sel_a = 0;
        e->sel_b = e->len;
        e->caret = e->len;
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_COPY:
        copy_selection(e);
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_CUT:
        if (!e->editable) {
            return TIKU_FIELD_HANDLED;
        }
        copy_selection(e);
        delete_selection(e);
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_PASTE: {
        size_t i;

        if (!e->editable) {
            return TIKU_FIELD_HANDLED;
        }

        delete_selection(e);
        for (i = 0; edit_clipboard[i] != '\0'; i++) {
            (void)tiku_field_key(e, TIKU_FIELD_CHAR, 0,
                                    edit_clipboard[i]);
        }
        return TIKU_FIELD_HANDLED;
    }

    case TIKU_FIELD_COMMIT:
        e->active = 0;
        return TIKU_FIELD_COMMITTED;

    case TIKU_FIELD_CANCEL:
        e->active = 0;
        return TIKU_FIELD_CANCELLED;

    default:
        return TIKU_FIELD_IGNORED;
    }
}
