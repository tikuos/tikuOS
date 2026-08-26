/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_form.c - reading a described panel, and drawing it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_form.h"
#include "tiku_font.h"
#include "tiku_layout.h"
#include "tiku_ui.h"

/** @brief Take the next blank-separated word of @p p into @p out. */
static const char *
word(const char *p, char *out, size_t max)
{
    size_t n = 0u;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    while (*p != '\0' && *p != '\n' && *p != ' ' && *p != '\t') {
        if (n + 1u < max) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
    return p;
}

/**
 * @brief The rest of the line, blanks trimmed off both ends.
 *
 * A label is words with spaces in them, so it cannot be read as one
 * word -- but it is the LAST thing on its line for exactly that reason.
 */
static const char *
tail(const char *p, char *out, size_t max)
{
    size_t n = 0u;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    while (*p != '\0' && *p != '\n') {
        if (n + 1u < max) {
            out[n++] = *p;
        }
        p++;
    }
    while (n > 0u && (out[n - 1u] == ' ' || out[n - 1u] == '\t' ||
                      out[n - 1u] == '\r')) {
        n--;
    }
    out[n] = '\0';
    return p;
}

static const char *
line_end(const char *p)
{
    while (*p != '\0' && *p != '\n') {
        p++;
    }
    return (*p == '\n') ? p + 1 : p;
}

int
tiku_form_parse(tiku_form_t *f, const char *text, int *unknown)
{
    const char *p = text;
    int skipped = 0;

    if (f == NULL) {
        return 0;
    }
    memset(f, 0, sizeof *f);
    if (unknown != NULL) {
        *unknown = 0;
    }
    if (text == NULL) {
        return 0;
    }
    while (*p != '\0') {
        char kind[16];
        const char *rest = word(p, kind, sizeof kind);
        tiku_form_row_t *row;

        if (kind[0] == '\0' || kind[0] == '#') {
            p = line_end(p);
            continue;               /* a blank line, or a remark */
        }
        if (strcmp(kind, "title") == 0) {
            (void)tail(rest, f->title, sizeof f->title);
            p = line_end(p);
            continue;
        }
        if (f->nrow >= TIKU_FORM_ROWS) {
            skipped++;
            p = line_end(p);
            continue;
        }
        row = &f->row[f->nrow];
        memset(row, 0, sizeof *row);
        if (strcmp(kind, "text") == 0) {
            row->kind = TIKU_FORM_TEXT;
            (void)tail(rest, row->label, sizeof row->label);
        } else if (strcmp(kind, "gauge") == 0) {
            char lo[16], hi[16];

            row->kind = TIKU_FORM_GAUGE;
            rest = word(rest, row->label, sizeof row->label);
            rest = word(rest, row->bind, sizeof row->bind);
            rest = word(rest, lo, sizeof lo);
            (void)word(rest, hi, sizeof hi);
            row->lo = atoi(lo);
            row->hi = (hi[0] != '\0') ? atoi(hi) : 100;
            if (row->hi <= row->lo) {
                row->hi = row->lo + 1;  /* a range of nothing shows nothing */
            }
        } else if (strcmp(kind, "toggle") == 0) {
            row->kind = TIKU_FORM_TOGGLE;
            rest = word(rest, row->label, sizeof row->label);
            (void)word(rest, row->bind, sizeof row->bind);
        } else if (strcmp(kind, "field") == 0) {
            row->kind = TIKU_FORM_FIELD;
            rest = word(rest, row->label, sizeof row->label);
            (void)word(rest, row->bind, sizeof row->bind);
        } else if (strcmp(kind, "button") == 0) {
            row->kind = TIKU_FORM_BUTTON;
            rest = word(rest, row->label, sizeof row->label);
            rest = word(rest, row->bind, sizeof row->bind);
            /* What pressing it writes.  A button with nothing to write
             * writes the one thing that always means "now": a 1. */
            (void)word(rest, row->value, sizeof row->value);
            if (row->value[0] == '\0') {
                snprintf(row->value, sizeof row->value, "1");
            }
            row->known = 1;
        } else {
            /* A row this build has never heard of.  Passed over rather
             * than refusing the panel: a device built later will have
             * rows this desktop does not know, and showing the ones it
             * does know is better than showing none of them. */
            skipped++;
            p = line_end(p);
            continue;
        }
        f->nrow++;
        p = line_end(p);
    }
    if (unknown != NULL) {
        *unknown = skipped;
    }
    return f->nrow;
}

/** @brief One row's height: a control's, with room to breathe. */
static int
row_h(void)
{
    return tiku_ui_row_h() + 6;
}

int
tiku_form_height(const tiku_form_t *f)
{
    return (f != NULL) ? f->nrow * row_h() + 12 : 0;
}

tiku_rect_t
tiku_form_row_rect(const tiku_form_t *f, tiku_rect_t body, int i)
{
    tiku_rect_t r = body;

    if (f == NULL || i < 0 || i >= f->nrow) {
        r.w = 0;
        r.h = 0;
        return r;
    }
    r.x = body.x + 6;
    r.y = body.y + 6 + i * row_h();
    r.w = body.w - 12;
    r.h = row_h();
    if (r.w < 0) {
        r.w = 0;
    }
    return r;
}

int
tiku_form_at(const tiku_form_t *f, tiku_rect_t body, int x, int y)
{
    int i;

    if (f == NULL) {
        return -1;
    }
    for (i = 0; i < f->nrow; i++) {
        tiku_rect_t r = tiku_form_row_rect(f, body, i);

        if (r.w > 0 && x >= r.x && x < r.x + r.w &&
            y >= r.y && y < r.y + r.h) {
            return i;
        }
    }
    return -1;
}

/** @brief A row's label half and control half, described once. */
static void
halves(tiku_rect_t row, tiku_rect_t *label, tiku_rect_t *control)
{
    tiku_layout_t l;

    tiku_layout_init(&l, 1, 8, 2);
    (void)tiku_layout_add(&l, 0, 2, 60);    /* the words        */
    (void)tiku_layout_add(&l, 0, 3, 80);    /* what they name   */
    *label = tiku_layout_slot(&l, row, 0);
    *control = tiku_layout_slot(&l, row, 1);
}

void
tiku_form_draw(const tiku_form_t *f, tiku_surface_t *s, tiku_rect_t body)
{
    const tiku_font_t *font = tiku_font_plain();
    int i;

    if (f == NULL || s == NULL) {
        return;
    }
    for (i = 0; i < f->nrow; i++) {
        const tiku_form_row_t *row = &f->row[i];
        tiku_rect_t r = tiku_form_row_rect(f, body, i);
        tiku_rect_t label, control;

        halves(r, &label, &control);
        if (row->kind == TIKU_FORM_TEXT) {
            tiku_text(s, font, r.x + 2,
                           r.y + (r.h - font->height) / 2 + font->ascent,
                           row->label, TIKU_C_TEXT);
            continue;
        }
        if (row->kind != TIKU_FORM_BUTTON) {
            tiku_text(s, font, label.x,
                           r.y + (r.h - font->height) / 2 + font->ascent,
                           row->label, TIKU_C_TEXT);
        }
        switch (row->kind) {
        case TIKU_FORM_GAUGE: {
            /*
             * A place that has not answered yet is drawn EMPTY rather
             * than at zero: those look the same and mean opposite
             * things, so the words say which.
             */
            int v = row->known ? atoi(row->value) : row->lo;
            float part = (float)(v - row->lo) /
                         (float)(row->hi - row->lo);

            if (part < 0.0f) {
                part = 0.0f;
            }
            if (part > 1.0f) {
                part = 1.0f;
            }
            tiku_ui_gauge(s, control, row->known ? part : 0.0f);
            break;
        }
        case TIKU_FORM_TOGGLE:
            tiku_ui_checkbox(s, control,
                (row->known && atoi(row->value) != 0) ? "on" : "off",
                (row->known && atoi(row->value) != 0) ? TIKU_S_ON : 0u);
            break;
        case TIKU_FORM_FIELD:
            tiku_ui_textfield(s, control,
                              row->known ? row->value : "", -1, 0u);
            break;
        case TIKU_FORM_BUTTON:
            tiku_ui_button(s, control, row->label, 0u);
            break;
        default:
            break;
        }
    }
}

const char *
tiku_form_press(tiku_form_t *f, int i)
{
    tiku_form_row_t *row;

    if (f == NULL || i < 0 || i >= f->nrow) {
        return NULL;
    }
    row = &f->row[i];
    if (row->bind[0] == '\0') {
        return NULL;            /* nothing to write to */
    }
    if (row->kind == TIKU_FORM_BUTTON) {
        return row->value;
    }
    if (row->kind == TIKU_FORM_TOGGLE) {
        /* The other of the two, from what the place last said -- so a
         * toggle whose place has not answered turns ON, which is what a
         * person pressing an off-looking switch means. */
        snprintf(row->value, sizeof row->value, "%d",
                 (row->known && atoi(row->value) != 0) ? 0 : 1);
        row->known = 1;
        return row->value;
    }
    return NULL;
}
