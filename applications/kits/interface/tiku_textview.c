/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_textview.c - the document's text, caret and scroll.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <string.h>

#include "tiku_textview.h"

/** @brief Replace one line's text with @p len bytes of @p text. */
static void
line_set(tiku_textview_line_t *l, const char *text, int len)
{
    char *copy;

    if (len < 0) {
        len = 0;
    }
    copy = malloc((size_t)len + 1u);
    if (copy == NULL) {
        return;                 /* the old text stands rather than a NULL */
    }
    if (len > 0 && text != NULL) {
        memcpy(copy, text, (size_t)len);
    }
    copy[len] = '\0';
    free(l->text);
    l->text = copy;
    l->len = len;
}

static void
touched(tiku_textview_t *tv)
{
    tv->modified = 1;
}

void
tiku_textview_init(tiku_textview_t *tv)
{
    if (tv == NULL) {
        return;
    }
    memset(tv, 0, sizeof *tv);
    /* One empty line, never zero: every operation below may assume the
     * caret has a line to stand on, which is what keeps them short. */
    line_set(&tv->line[0], "", 0);
    tv->nline = 1;
}

void
tiku_textview_free(tiku_textview_t *tv)
{
    int i;

    if (tv == NULL) {
        return;
    }
    for (i = 0; i < tv->nline; i++) {
        free(tv->line[i].text);
        tv->line[i].text = NULL;
        tv->line[i].len = 0;
    }
    tv->nline = 0;
    tiku_textview_init(tv);
}

void
tiku_textview_set(tiku_textview_t *tv, const char *text)
{
    const char *p, *nl;

    if (tv == NULL) {
        return;
    }
    tiku_textview_free(tv);
    tv->nline = 0;
    p = (text != NULL) ? text : "";
    for (;;) {
        int len;

        nl = strchr(p, '\n');
        len = (nl != NULL) ? (int)(nl - p) : (int)strlen(p);
        if (len > TIKU_TEXTVIEW_LINE_MAX - 1) {
            len = TIKU_TEXTVIEW_LINE_MAX - 1;
        }
        if (tv->nline >= TIKU_TEXTVIEW_LINES_MAX) {
            break;
        }
        memset(&tv->line[tv->nline], 0, sizeof tv->line[0]);
        line_set(&tv->line[tv->nline], p, len);
        tv->nline++;
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
        if (*p == '\0') {
            /* The newline that ENDS a file closes the last line rather
             * than starting an empty one, or a file grows a blank line
             * every time it is opened and written back. */
            break;
        }
    }
    if (tv->nline == 0) {
        line_set(&tv->line[0], "", 0);
        tv->nline = 1;
    }
    tv->cy = 0;
    tv->cx = 0;
    tv->top = 0;
    tv->modified = 0;
}

int
tiku_textview_lines(const tiku_textview_t *tv)
{
    return (tv != NULL && tv->nline > 0) ? tv->nline : 0;
}

const char *
tiku_textview_line(const tiku_textview_t *tv, int i)
{
    if (tv == NULL || i < 0 || i >= tv->nline ||
        tv->line[i].text == NULL) {
        return "";
    }
    return tv->line[i].text;
}

int
tiku_textview_line_len(const tiku_textview_t *tv, int i)
{
    if (tv == NULL || i < 0 || i >= tv->nline) {
        return 0;
    }
    return tv->line[i].len;
}

int
tiku_textview_modified(const tiku_textview_t *tv)
{
    return (tv != NULL) ? tv->modified : 0;
}

void
tiku_textview_saved(tiku_textview_t *tv)
{
    if (tv != NULL) {
        tv->modified = 0;
    }
}

void
tiku_textview_caret(const tiku_textview_t *tv, int *line, int *col)
{
    if (line != NULL) { *line = (tv != NULL) ? tv->cy : 0; }
    if (col != NULL)  { *col  = (tv != NULL) ? tv->cx : 0; }
}

int
tiku_textview_top(const tiku_textview_t *tv)
{
    return (tv != NULL) ? tv->top : 0;
}

void
tiku_textview_place(tiku_textview_t *tv, int line, int col)
{
    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    if (line < 0) { line = 0; }
    if (line >= tv->nline) { line = tv->nline - 1; }
    tv->cy = line;
    if (col < 0) { col = 0; }
    if (col > tv->line[line].len) { col = tv->line[line].len; }
    tv->cx = col;
}

void
tiku_textview_insert(tiku_textview_t *tv, char ch)
{
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    tiku_textview_line_t *l;

    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    l = &tv->line[tv->cy];
    if (l->len + 1 >= TIKU_TEXTVIEW_LINE_MAX) {
        return;                 /* a full line refuses rather than truncates */
    }
    memcpy(buf, l->text, (size_t)tv->cx);
    buf[tv->cx] = ch;
    memcpy(buf + tv->cx + 1, l->text + tv->cx,
           (size_t)(l->len - tv->cx));
    line_set(l, buf, l->len + 1);
    tv->cx++;
    touched(tv);
}

void
tiku_textview_newline(tiku_textview_t *tv)
{
    tiku_textview_line_t *l;
    int tail, i;

    if (tv == NULL || tv->nline + 1 >= TIKU_TEXTVIEW_LINES_MAX) {
        return;
    }
    l = &tv->line[tv->cy];
    tail = l->len - tv->cx;
    for (i = tv->nline; i > tv->cy + 1; i--) {
        tv->line[i] = tv->line[i - 1];
    }
    memset(&tv->line[tv->cy + 1], 0, sizeof tv->line[0]);
    line_set(&tv->line[tv->cy + 1], l->text + tv->cx, tail);
    line_set(l, l->text, tv->cx);
    tv->nline++;
    tv->cy++;
    tv->cx = 0;
    touched(tv);
}

/** @brief Fold line @p at into the one before it, caret at the seam. */
static void
join_previous(tiku_textview_t *tv, int at)
{
    tiku_textview_line_t *prev = &tv->line[at - 1];
    tiku_textview_line_t *cur = &tv->line[at];
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    int seam = prev->len;
    int i;

    if (seam + cur->len >= TIKU_TEXTVIEW_LINE_MAX) {
        return;
    }
    memcpy(buf, prev->text, (size_t)seam);
    memcpy(buf + seam, cur->text, (size_t)cur->len);
    line_set(prev, buf, seam + cur->len);
    free(cur->text);
    for (i = at; i < tv->nline - 1; i++) {
        tv->line[i] = tv->line[i + 1];
    }
    memset(&tv->line[tv->nline - 1], 0, sizeof tv->line[0]);
    tv->nline--;
    tv->cy = at - 1;
    tv->cx = seam;
    touched(tv);
}

void
tiku_textview_backspace(tiku_textview_t *tv)
{
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    tiku_textview_line_t *l;

    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    l = &tv->line[tv->cy];
    if (tv->cx > 0) {
        memcpy(buf, l->text, (size_t)(tv->cx - 1));
        memcpy(buf + tv->cx - 1, l->text + tv->cx,
               (size_t)(l->len - tv->cx));
        line_set(l, buf, l->len - 1);
        tv->cx--;
        touched(tv);
    } else if (tv->cy > 0) {
        join_previous(tv, tv->cy);
    }
}

void
tiku_textview_delete(tiku_textview_t *tv)
{
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    tiku_textview_line_t *l;

    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    l = &tv->line[tv->cy];
    if (tv->cx < l->len) {
        memcpy(buf, l->text, (size_t)tv->cx);
        memcpy(buf + tv->cx, l->text + tv->cx + 1,
               (size_t)(l->len - tv->cx - 1));
        line_set(l, buf, l->len - 1);
        touched(tv);
    } else if (tv->cy + 1 < tv->nline) {
        /* Forwards at a line's end is the same fold as backspace at the
         * next line's head, so it is the same code -- and the caret must
         * come back to where it already was, which join leaves it. */
        join_previous(tv, tv->cy + 1);
    }
}

void
tiku_textview_move(tiku_textview_t *tv, tiku_textview_move_t how)
{
    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    switch (how) {
    case TIKU_TEXTVIEW_LEFT:
        if (tv->cx > 0) {
            tv->cx--;
        } else if (tv->cy > 0) {
            tv->cy--;
            tv->cx = tv->line[tv->cy].len;
        }
        break;
    case TIKU_TEXTVIEW_RIGHT:
        if (tv->cx < tv->line[tv->cy].len) {
            tv->cx++;
        } else if (tv->cy + 1 < tv->nline) {
            tv->cy++;
            tv->cx = 0;
        }
        break;
    case TIKU_TEXTVIEW_UP:
        if (tv->cy > 0) {
            tv->cy--;
        }
        break;
    case TIKU_TEXTVIEW_DOWN:
        if (tv->cy + 1 < tv->nline) {
            tv->cy++;
        }
        break;
    case TIKU_TEXTVIEW_HOME:
        tv->cx = 0;
        return;
    case TIKU_TEXTVIEW_END:
        tv->cx = tv->line[tv->cy].len;
        return;
    default:
        return;
    }
    /* Up and down keep the column where they can: a short line clamps
     * the caret to its end rather than off it. */
    if (tv->cx > tv->line[tv->cy].len) {
        tv->cx = tv->line[tv->cy].len;
    }
}

int
tiku_textview_reveal(tiku_textview_t *tv, int rows)
{
    int was;

    if (tv == NULL || rows <= 0) {
        return 0;
    }
    was = tv->top;
    if (tv->cy < tv->top) {
        tv->top = tv->cy;
    }
    if (tv->cy >= tv->top + rows) {
        tv->top = tv->cy - rows + 1;
    }
    if (tv->top < 0) {
        tv->top = 0;
    }
    return tv->top != was;
}

int
tiku_textview_scroll_to(tiku_textview_t *tv, int top, int rows)
{
    int was, most;

    if (tv == NULL) {
        return 0;
    }
    was = tv->top;
    /* Never past the last screenful: scrolling into blank space below a
     * document is a way to lose sight of it. */
    most = (rows > 0 && tv->nline > rows) ? tv->nline - rows : 0;
    if (top < 0) { top = 0; }
    if (top > most) { top = most; }
    tv->top = top;
    return tv->top != was;
}

size_t
tiku_textview_text(const tiku_textview_t *tv, char *out, size_t max)
{
    size_t need = 0;
    int i;

    if (tv == NULL) {
        return 0;
    }
    for (i = 0; i < tv->nline; i++) {
        size_t len = (size_t)tv->line[i].len;

        if (out != NULL && need + len + 1u < max) {
            memcpy(out + need, tv->line[i].text, len);
            out[need + len] = '\n';
        }
        need += len + 1u;
    }
    if (out != NULL && max > 0) {
        out[(need < max) ? need : max - 1u] = '\0';
    }
    return need;
}
