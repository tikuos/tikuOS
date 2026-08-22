/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit.c - a text editor, as one application descriptor.
 *
 * Lines are separate allocations so an edit touches one of them; the
 * caret is a line and a column into that text, never a pixel.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_gfx.h"

#define EDIT_W      560
#define EDIT_H      400
#define STRIP_H     20
#define MARGIN      6
#define LINES_MAX   4000
#define LINE_MAX    1024

#define CMD_SAVE   1
#define CMD_REVERT 2
#define CMD_QUIT   3

typedef struct {
    char *text;                 /* NUL-terminated, owned */
    int   len;
} line_t;

typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    uint32_t                        id;
    line_t                          line[LINES_MAX];
    int                             nline;
    int                             cy, cx;     /* caret line, column */
    int                             top;        /* first line drawn   */
    int                             modified;
    int                             saved_shown;
    char                            path[512];
    char                            note[128];
} edit_state_t;

/* The file named on the command line, before any state exists. */
static char edit_path[512];

static int
rows_visible(void)
{
    const tiku_font_t *f = tiku_font_plain();

    return (EDIT_H - STRIP_H - MARGIN) / (f->height + 2);
}

static void
line_set(line_t *l, const char *text, int len)
{
    char *copy = malloc((size_t)len + 1u);

    if (copy == NULL) {
        return;
    }
    memcpy(copy, text, (size_t)len);
    copy[len] = '\0';
    free(l->text);
    l->text = copy;
    l->len = len;
}

static void
lines_free(edit_state_t *st)
{
    int i;

    for (i = 0; i < st->nline; i++) {
        free(st->line[i].text);
        st->line[i].text = NULL;
        st->line[i].len = 0;
    }
    st->nline = 0;
}

/** @brief Read @p path into the buffer.  A missing file is an empty one. */
static void
load(edit_state_t *st)
{
    FILE *f = fopen(st->path, "r");
    char buf[LINE_MAX];

    lines_free(st);
    if (f != NULL) {
        while (st->nline < LINES_MAX &&
               fgets(buf, sizeof buf, f) != NULL) {
            int len = (int)strlen(buf);

            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
                len--;
            }
            line_set(&st->line[st->nline++], buf, len);
        }
        (void)fclose(f);
        snprintf(st->note, sizeof st->note, "%d line%s", st->nline,
                 st->nline == 1 ? "" : "s");
    } else {
        snprintf(st->note, sizeof st->note, "new file");
    }
    if (st->nline == 0) {
        line_set(&st->line[st->nline++], "", 0);
    }
    st->cy = 0;
    st->cx = 0;
    st->top = 0;
    st->modified = 0;
}

/** @brief @return nonzero when every line reached the disk. */
static int
save(edit_state_t *st)
{
    FILE *f;
    int i;

    if (st->path[0] == '\0') {
        snprintf(st->note, sizeof st->note, "no file name to save to");
        return 0;
    }
    f = fopen(st->path, "w");
    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "cannot write %s", st->path);
        return 0;
    }
    for (i = 0; i < st->nline; i++) {
        (void)fprintf(f, "%s\n", st->line[i].text);
    }
    /* The close is what can still fail -- a full disk reports here and
     * nowhere earlier -- so the saved state waits for it. */
    if (fclose(f) != 0) {
        snprintf(st->note, sizeof st->note, "could not finish writing");
        return 0;
    }
    st->modified = 0;
    snprintf(st->note, sizeof st->note, "saved %d line%s", st->nline,
             st->nline == 1 ? "" : "s");
    return 1;
}

static void
scroll_to_caret(edit_state_t *st)
{
    int rows = rows_visible();

    if (st->cy < st->top) {
        st->top = st->cy;
    }
    if (st->cy >= st->top + rows) {
        st->top = st->cy - rows + 1;
    }
    if (st->top < 0) {
        st->top = 0;
    }
}

static void
paint(edit_state_t *st)
{
    const tiku_font_t *f = tiku_font_plain();
    const tiku_font_t *small = tiku_font_at(11);
    int step = f->height + 2;
    int rows = rows_visible();
    tiku_rect_t page = { 0, 0, EDIT_W, EDIT_H - STRIP_H };
    tiku_rect_t strip = { 0, EDIT_H - STRIP_H, EDIT_W, STRIP_H };
    char where[128];
    int i;

    tiku_fill(st->surface, page, TIKU_C_DOC);
    for (i = 0; i < rows && st->top + i < st->nline; i++) {
        int y = MARGIN + i * step;

        tiku_text(st->surface, f, MARGIN, y + f->ascent,
                       st->line[st->top + i].text, TIKU_C_TEXT);
    }
    {
        /* The caret sits where the text before it ends, which is the only
         * way a proportional face can place it. */
        int row = st->cy - st->top;
        char head[LINE_MAX];
        int cut = st->cx;

        if (row >= 0 && row < rows) {
            if (cut > st->line[st->cy].len) {
                cut = st->line[st->cy].len;
            }
            memcpy(head, st->line[st->cy].text, (size_t)cut);
            head[cut] = '\0';
            tiku_vline(st->surface,
                            MARGIN + tiku_text_width(f, head),
                            MARGIN + row * step, step, TIKU_C_TEXT);
        }
    }
    tiku_fill(st->surface, strip, TIKU_C_PANEL);
    tiku_hline(st->surface, 0, strip.y, EDIT_W,
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    /* Marked in ASCII: the interface face carries no bullet, and a
     * modified file that says nothing about it is the worst of the
     * three states this line can be in. */
    snprintf(where, sizeof where, "%s%s  %d:%d  %s",
             st->modified ? "* " : "",
             st->path[0] != '\0' ? st->path : "untitled",
             st->cy + 1, st->cx + 1, st->note);
    tiku_text(st->surface, small, MARGIN,
                   strip.y + (STRIP_H - small->height) / 2 + small->ascent,
                   where, TIKU_C_TEXT);
    (void)st->services->frame(st->services->ctx, st->id, st->surface->px,
                              EDIT_W, EDIT_H);
}

static void
publish(edit_state_t *st)
{
    tiku_menuset_t menus;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "File");
    menus.menu[0].nitem = 3;
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "Save");
    menus.menu[0].item[0].command = CMD_SAVE;
    menus.menu[0].item[0].sc = 's';
    /* Offered disabled with nothing to save, so the row keeps its place
     * and its shortcut stays discoverable. */
    menus.menu[0].item[0].enabled = (unsigned char)st->modified;
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Revert");
    menus.menu[0].item[1].command = CMD_REVERT;
    menus.menu[0].item[1].enabled = (unsigned char)st->modified;
    snprintf(menus.menu[0].item[2].label,
             sizeof menus.menu[0].item[2].label, "Quit");
    menus.menu[0].item[2].command = CMD_QUIT;
    menus.menu[0].item[2].enabled = 1;
    (void)st->services->menus(st->services->ctx, st->id, &menus);
}

static void
touched(edit_state_t *st)
{
    int was = st->modified;

    st->modified = 1;
    st->note[0] = '\0';
    if (!was) {
        publish(st);            /* Save and Revert become reachable */
    }
}

static void
insert_char(edit_state_t *st, char c)
{
    line_t *l = &st->line[st->cy];
    char buf[LINE_MAX];

    if (l->len + 1 >= LINE_MAX) {
        return;
    }
    memcpy(buf, l->text, (size_t)st->cx);
    buf[st->cx] = c;
    memcpy(buf + st->cx + 1, l->text + st->cx,
           (size_t)(l->len - st->cx));
    line_set(l, buf, l->len + 1);
    st->cx++;
    touched(st);
}

static void
split_line(edit_state_t *st)
{
    line_t *l = &st->line[st->cy];
    int tail = l->len - st->cx;
    int i;

    if (st->nline + 1 >= LINES_MAX) {
        return;
    }
    for (i = st->nline; i > st->cy + 1; i--) {
        st->line[i] = st->line[i - 1];
    }
    memset(&st->line[st->cy + 1], 0, sizeof st->line[0]);
    line_set(&st->line[st->cy + 1], l->text + st->cx, tail);
    line_set(l, l->text, st->cx);
    st->nline++;
    st->cy++;
    st->cx = 0;
    touched(st);
}

static void
join_previous(edit_state_t *st)
{
    line_t *prev = &st->line[st->cy - 1];
    line_t *cur = &st->line[st->cy];
    char buf[LINE_MAX];
    int at = prev->len;
    int i;

    if (at + cur->len >= LINE_MAX) {
        return;
    }
    memcpy(buf, prev->text, (size_t)at);
    memcpy(buf + at, cur->text, (size_t)cur->len);
    line_set(prev, buf, at + cur->len);
    free(cur->text);
    for (i = st->cy; i < st->nline - 1; i++) {
        st->line[i] = st->line[i + 1];
    }
    memset(&st->line[st->nline - 1], 0, sizeof st->line[0]);
    st->nline--;
    st->cy--;
    st->cx = at;
    touched(st);
}

static void
backspace(edit_state_t *st)
{
    line_t *l = &st->line[st->cy];
    char buf[LINE_MAX];

    if (st->cx > 0) {
        memcpy(buf, l->text, (size_t)(st->cx - 1));
        memcpy(buf + st->cx - 1, l->text + st->cx,
               (size_t)(l->len - st->cx));
        line_set(l, buf, l->len - 1);
        st->cx--;
        touched(st);
    } else if (st->cy > 0) {
        join_previous(st);
    }
}

static void
clamp_column(edit_state_t *st)
{
    if (st->cx > st->line[st->cy].len) {
        st->cx = st->line[st->cy].len;
    }
}

static int
edit_start(void **state, const tiku_app_services_t *services)
{
    edit_state_t *st = calloc(1, sizeof *st);
    const char *slash;
    char title[64];

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->surface = tiku_surface_new(EDIT_W, EDIT_H, TIKU_C_DOC);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    snprintf(st->path, sizeof st->path, "%s", edit_path);
    load(st);
    slash = strrchr(st->path, '/');
    snprintf(title, sizeof title, "%s",
             (slash != NULL) ? slash + 1
             : (st->path[0] != '\0') ? st->path : "Edit");
    st->id = services->open(services->ctx, title, EDIT_W, EDIT_H);
    paint(st);
    publish(st);
    *state = st;
    return 0;
}

static void
edit_stop(void *state)
{
    edit_state_t *st = state;

    if (st != NULL) {
        lines_free(st);
        tiku_surface_free(st->surface);
        free(st);
    }
}

static int
edit_event(void *state, const tiku_event_t *event)
{
    edit_state_t *st = state;
    int rows = rows_visible();

    if (event->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    if ((event->modifiers & TIKU_MOD_CMD) != 0u) {
        if (event->key == 's' || event->key == 'S') {
            (void)save(st);
            publish(st);
            paint(st);
        }
        return 0;
    }
    switch (event->key) {
    case TIKU_KEY_ESCAPE:
        return 1;
    case TIKU_KEY_RETURN:
        split_line(st);
        break;
    case TIKU_KEY_BACKSPACE:
        backspace(st);
        break;
    case TIKU_KEY_LEFT:
        if (st->cx > 0) {
            st->cx--;
        } else if (st->cy > 0) {
            st->cx = st->line[--st->cy].len;
        }
        break;
    case TIKU_KEY_RIGHT:
        if (st->cx < st->line[st->cy].len) {
            st->cx++;
        } else if (st->cy + 1 < st->nline) {
            st->cy++;
            st->cx = 0;
        }
        break;
    case TIKU_KEY_UP:
        if (st->cy > 0) {
            st->cy--;
            clamp_column(st);
        }
        break;
    case TIKU_KEY_DOWN:
        if (st->cy + 1 < st->nline) {
            st->cy++;
            clamp_column(st);
        }
        break;
    case TIKU_KEY_HOME:
        st->cx = 0;
        break;
    case TIKU_KEY_END:
        st->cx = st->line[st->cy].len;
        break;
    case TIKU_KEY_PAGE_UP:
        st->cy = (st->cy > rows) ? st->cy - rows : 0;
        clamp_column(st);
        break;
    case TIKU_KEY_PAGE_DOWN:
        st->cy = (st->cy + rows < st->nline) ? st->cy + rows
                                             : st->nline - 1;
        clamp_column(st);
        break;
    default:
        if (event->key >= 32u && event->key < 127u) {
            insert_char(st, (char)event->key);
        } else {
            return 0;           /* not ours: no repaint owed */
        }
        break;
    }
    scroll_to_caret(st);
    paint(st);
    return 0;
}

static int
edit_pick(void *state, uint32_t window, int command)
{
    edit_state_t *st = state;

    (void)window;
    switch (command) {
    case CMD_QUIT:
        return 1;
    case CMD_SAVE:
        (void)save(st);
        break;
    case CMD_REVERT:
        load(st);
        break;
    default:
        return 0;
    }
    publish(st);
    paint(st);
    return 0;
}

const tiku_app_descriptor_t tiku_edit_app = {
    .id = "org.tikuos.edit",
    .name = "Edit",
    .start = edit_start,
    .stop = edit_stop,
    .event = edit_event,
    .pick = edit_pick
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_edit_app
};
#endif

#ifndef TIKU_APP_EMBED
int
main(int argc, char **argv)
{
    if (argc > 1) {
        snprintf(edit_path, sizeof edit_path, "%s", argv[1]);
    }
    return tiku_client_run(&tiku_edit_app);
}
#endif
