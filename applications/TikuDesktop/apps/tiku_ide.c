/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ide.c - a place to work on a PROGRAM rather than on a file.
 *
 * The project window lists what the program is made of, grouped the way
 * the project file groups it; opening a row opens an editor window of
 * its own, and the Window menu reaches every one of them.  A file
 * already open is RAISED rather than opened twice, which is the whole
 * difference between windows an application owns and windows it merely
 * makes.
 *
 * The simple editor stays what it is.  This is not a bigger one: it is
 * the same kit machinery -- a text view, the syntax tables, the shell's
 * own file panel -- held in several windows at once.
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
#include "tiku_list.h"
#include "tiku_syntax.h"
#include "tiku_textview.h"
#include "tiku_ui.h"

#define IDE_W      320
#define IDE_H      300
#define EDIT_W     460
#define EDIT_H     300
#define STRIP_H    22
#define MARGIN     6

#define IDE_ROWS   64
#define IDE_EDITS  3            /* what a session may hold beside the
                                 * project window */

#define CMD_OPEN     1
#define CMD_QUIT     2
#define CMD_SAVE     3
#define CMD_SHUT     4
#define CMD_WINDOW   64         /* +i: the i'th editor of the Window menu */

/** @brief One line of the project window: a file, or a group's name. */
typedef struct {
    char name[64];
    char path[512];             /* empty for a heading */
    int  heading;
} prow_t;

typedef struct {
    char  title[64];
    char  dir[512];             /* what the file's own paths are from */
    int   nrow;
    int   unknown;              /* lines this build did not know */
    prow_t row[IDE_ROWS];
} project_t;

/** @brief One editor window: its file, its text, and its window. */
typedef struct {
    uint32_t             id;
    tiku_surface_t *surface;    /* NULL when the seat is free */
    tiku_textview_t tv;
    tiku_syntax_lang_t   lang;
    char                 path[512];
    char                 name[64];
} edit_t;

typedef struct {
    const tiku_app_services_t *services;
    uint32_t                        proj_id;
    tiku_surface_t            *proj_surface;
    tiku_list_t                tv_list;
    project_t                       proj;
    edit_t                          edit[IDE_EDITS];
    char                            note[128];
} ide_t;

static void project_paint(ide_t *st);
static void project_publish(ide_t *st);

/*---------------------------------------------------------------------------*/
/* The project file                                                          */
/*---------------------------------------------------------------------------*/

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

/** @brief The rest of the line, blanks trimmed off both ends. */
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

/**
 * @brief Read a project description into @p pj.
 *
 * A line this build does not know is skipped and counted rather than
 * refusing the project: the lines a BUILD will need were written before
 * anything here could act on them, and a project that opens showing
 * what it can is worth more than one that refuses to open at all.
 *
 * @return the number of rows read.
 */
static int
project_parse(project_t *pj, const char *text, const char *dir)
{
    const char *p = text;

    memset(pj, 0, sizeof *pj);
    snprintf(pj->dir, sizeof pj->dir, "%s", (dir != NULL) ? dir : ".");
    if (text == NULL) {
        return 0;
    }
    while (*p != '\0') {
        char kind[16];
        const char *rest = word(p, kind, sizeof kind);
        prow_t *row;

        if (kind[0] == '\0' || kind[0] == '#') {
            p = line_end(p);
            continue;               /* a blank line, or a remark */
        }
        if (strcmp(kind, "title") == 0) {
            (void)tail(rest, pj->title, sizeof pj->title);
            p = line_end(p);
            continue;
        }
        if (pj->nrow >= IDE_ROWS) {
            pj->unknown++;
            p = line_end(p);
            continue;
        }
        row = &pj->row[pj->nrow];
        memset(row, 0, sizeof *row);
        if (strcmp(kind, "group") == 0) {
            row->heading = 1;
            (void)tail(rest, row->name, sizeof row->name);
        } else if (strcmp(kind, "file") == 0) {
            char at[sizeof row->path];

            (void)word(rest, row->name, sizeof row->name);
            /* Paths are from the project's own directory, so a project
             * is a FOLDER somebody can move.  Built aside and copied,
             * because the two halves live in the same object. */
            snprintf(at, sizeof at, "%s/%s", pj->dir, row->name);
            snprintf(row->path, sizeof row->path, "%s", at);
        } else {
            pj->unknown++;
            p = line_end(p);
            continue;
        }
        pj->nrow++;
        p = line_end(p);
    }
    return pj->nrow;
}

/*---------------------------------------------------------------------------*/
/* Editors                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief The editor holding @p path, or NULL. */
static edit_t *
edit_of_path(ide_t *st, const char *path)
{
    int i;

    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface != NULL &&
            strcmp(st->edit[i].path, path) == 0) {
            return &st->edit[i];
        }
    }
    return NULL;
}

/** @brief The editor in window @p id, or NULL. */
static edit_t *
edit_of_id(ide_t *st, uint32_t id)
{
    int i;

    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface != NULL && st->edit[i].id == id) {
            return &st->edit[i];
        }
    }
    return NULL;
}

/** @brief What a name says the language is; only the name can say. */
static tiku_syntax_lang_t
lang_of(const char *name)
{
    const char *dot = strrchr(name, '.');

    return (dot != NULL && strcmp(dot, ".bas") == 0) ? TIKU_SYNTAX_BASIC
                                                     : TIKU_SYNTAX_NONE;
}

static void
edit_paint(ide_t *st, edit_t *e)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t page = { 0, 0, EDIT_W, EDIT_H - STRIP_H };
    tiku_rect_t strip = { 0, EDIT_H - STRIP_H, EDIT_W, STRIP_H };
    int rows = (EDIT_H - STRIP_H - MARGIN) / (f->height + 2);
    int i;
    char where[160];

    if (e->surface == NULL) {
        return;
    }
    tiku_ui_sunken(e->surface, page, TIKU_C_DOC);
    for (i = 0; i < rows; i++) {
        int line = tiku_textview_top(&e->tv) + i;
        const char *text = tiku_textview_line(&e->tv, line);
        int y = MARGIN + i * (f->height + 2);

        if (text == NULL) {
            break;
        }
        if (e->lang != TIKU_SYNTAX_NONE) {
            tiku_span_t span[64];
            int n = tiku_syntax_spans(e->lang, text, span,
                                      (int)(sizeof span / sizeof span[0]));

            (void)tiku_ui_text_spans(e->surface, f, MARGIN, y + f->ascent,
                                     text, span, n);
        } else {
            tiku_text(e->surface, f, MARGIN, y + f->ascent, text,
                           TIKU_C_TEXT);
        }
    }
    tiku_fill(e->surface, strip, TIKU_C_PANEL);
    snprintf(where, sizeof where, "%s%s  %d lines", e->name,
             tiku_textview_modified(&e->tv) ? " (changed)" : "",
             tiku_textview_lines(&e->tv));
    tiku_text(e->surface, f, MARGIN,
                   strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                   where, TIKU_C_TEXT);
    (void)st->services->frame(st->services->ctx, e->id, e->surface->px,
                              EDIT_W, EDIT_H);
}

static void
edit_publish(ide_t *st, edit_t *e)
{
    tiku_menuset_t menus;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "File");
    menus.menu[0].nitem = 2;
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "Save %s", e->name);
    menus.menu[0].item[0].command = CMD_SAVE;
    menus.menu[0].item[0].enabled =
        (unsigned char)tiku_textview_modified(&e->tv);
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Close %s", e->name);
    menus.menu[0].item[1].command = CMD_QUIT;
    menus.menu[0].item[1].enabled = 1;
    (void)st->services->menus(st->services->ctx, e->id, &menus);
}

/** @brief Read @p e's file in.  A missing file is an empty one. */
static void
edit_load(edit_t *e)
{
    static char whole[TIKU_TEXTVIEW_LINES_MAX * 8];
    FILE *f = fopen(e->path, "r");

    tiku_textview_init(&e->tv);
    if (f == NULL) {
        tiku_textview_set(&e->tv, "");
        return;
    }
    {
        size_t got = fread(whole, 1u, sizeof whole - 1u, f);

        whole[got] = '\0';
    }
    (void)fclose(f);
    tiku_textview_set(&e->tv, whole);
}

static int
edit_save(edit_t *e)
{
    FILE *f = fopen(e->path, "w");
    int i;

    if (f == NULL) {
        return 0;
    }
    for (i = 0; i < tiku_textview_lines(&e->tv); i++) {
        (void)fprintf(f, "%s\n", tiku_textview_line(&e->tv, i));
    }
    if (fclose(f) != 0) {
        return 0;
    }
    tiku_textview_saved(&e->tv);
    return 1;
}

/**
 * @brief Open @p row's file, or bring its window forward.
 *
 * A file already open is RAISED: opening it twice would give a person
 * two windows onto one document, and whichever they typed into last
 * would be the one the other quietly forgot.
 */
static void
edit_open(ide_t *st, const prow_t *row)
{
    edit_t *e = edit_of_path(st, row->path);
    int i;

    if (e != NULL) {
        if (st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx, e->id);
        }
        return;
    }
    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface == NULL) {
            e = &st->edit[i];
            break;
        }
    }
    if (e == NULL) {
        snprintf(st->note, sizeof st->note,
                 "no room for another window");
        project_paint(st);
        return;
    }
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s", row->path);
    snprintf(e->name, sizeof e->name, "%s", row->name);
    e->lang = lang_of(e->name);
    edit_load(e);
    e->surface = tiku_surface_new(EDIT_W, EDIT_H, TIKU_C_PANEL);
    if (e->surface == NULL) {
        return;
    }
    e->id = st->services->open(st->services->ctx, e->name, EDIT_W,
                               EDIT_H);
    if (e->id == 0u) {
        tiku_surface_free(e->surface);
        e->surface = NULL;
        snprintf(st->note, sizeof st->note, "no room for another window");
        project_paint(st);
        return;
    }
    edit_paint(st, e);
    edit_publish(st, e);
    project_paint(st);          /* the Window menu grew */
    project_publish(st);
}

/** @brief Let one editor go; the project window stays. */
static void
edit_drop(ide_t *st, edit_t *e)
{
    st->services->close(st->services->ctx, e->id);
    tiku_surface_free(e->surface);
    e->surface = NULL;
    project_paint(st);
    project_publish(st);
}

/*---------------------------------------------------------------------------*/
/* The project window                                                        */
/*---------------------------------------------------------------------------*/

static tiku_rect_t
project_body(void)
{
    tiku_rect_t r = { MARGIN, MARGIN, IDE_W - 2 * MARGIN,
                           IDE_H - STRIP_H - 2 * MARGIN };

    return r;
}

static const char *
row_name(void *ctx, int row)
{
    ide_t *st = ctx;

    return (row >= 0 && row < st->proj.nrow) ? st->proj.row[row].name
                                             : NULL;
}

static void
project_paint(ide_t *st)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t all = { 0, 0, IDE_W, IDE_H };
    tiku_rect_t strip = { 0, IDE_H - STRIP_H, IDE_W, STRIP_H };
    tiku_rect_t body = project_body();
    char says[160];

    if (st->proj_surface == NULL) {
        return;
    }
    tiku_fill(st->proj_surface, all, TIKU_C_PANEL);
    tiku_ui_sunken(st->proj_surface, body, TIKU_C_DOC);
    tiku_list_draw(&st->tv_list, st->proj_surface, body, row_name, st);
    tiku_fill(st->proj_surface, strip, TIKU_C_PANEL);
    if (st->note[0] != '\0') {
        snprintf(says, sizeof says, "%s", st->note);
    } else if (st->proj.nrow == 0) {
        snprintf(says, sizeof says, "no project open");
    } else {
        snprintf(says, sizeof says, "%d file%s", st->proj.nrow,
                 st->proj.nrow == 1 ? "" : "s");
    }
    tiku_text(st->proj_surface, f, MARGIN,
                   strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                   says, TIKU_C_TEXT);
    (void)st->services->frame(st->services->ctx, st->proj_id,
                              st->proj_surface->px, IDE_W, IDE_H);
}

/**
 * @brief The project window's menus, the Window menu among them.
 *
 * A row per open editor, marked when what it holds is unsaved -- so the
 * one place that lists the windows is also the one place that says
 * which of them has work in it.
 */
static void
project_publish(ide_t *st)
{
    tiku_menuset_t menus;
    int i, n = 0;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 2;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Project");
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "Open Project…");
    menus.menu[0].item[0].command = CMD_OPEN;
    menus.menu[0].item[0].enabled = 1;
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Close Project");
    menus.menu[0].item[1].command = CMD_SHUT;
    menus.menu[0].item[1].enabled = 1;
    menus.menu[0].nitem = 2;

    snprintf(menus.menu[1].title, sizeof menus.menu[1].title, "Window");
    for (i = 0; i < IDE_EDITS && n < TIKU_MENUSET_ITEMS; i++) {
        edit_t *e = &st->edit[i];

        if (e->surface == NULL) {
            continue;
        }
        /* The mark is the DOCUMENT's, not the window's: a person
         * looking for where their unsaved work is should find it in the
         * list of windows rather than by opening each one. */
        snprintf(menus.menu[1].item[n].label,
                 sizeof menus.menu[1].item[n].label, "%s%s", e->name,
                 tiku_textview_modified(&e->tv) ? " (changed)" : "");
        menus.menu[1].item[n].command = CMD_WINDOW + i;
        menus.menu[1].item[n].enabled = 1;
        n++;
    }
    if (n == 0) {
        snprintf(menus.menu[1].item[0].label,
                 sizeof menus.menu[1].item[0].label, "No open files");
        menus.menu[1].item[0].command = 0;
        menus.menu[1].item[0].enabled = 0;
        n = 1;
    }
    menus.menu[1].nitem = n;
    (void)st->services->menus(st->services->ctx, st->proj_id, &menus);
}

/** @brief Show @p pj: the rows, with the groups among them as headings. */
static void
project_show(ide_t *st)
{
    int i;

    tiku_list_init(&st->tv_list, 1);
    (void)tiku_list_set_count(&st->tv_list, st->proj.nrow);
    for (i = 0; i < st->proj.nrow; i++) {
        tiku_list_set_heading(&st->tv_list, i, st->proj.row[i].heading);
    }
    st->note[0] = '\0';
    if (st->proj.unknown > 0) {
        snprintf(st->note, sizeof st->note,
                 "%d line%s this build does not know", st->proj.unknown,
                 st->proj.unknown == 1 ? "" : "s");
    }
    project_paint(st);
    project_publish(st);
}

/** @brief Open what the person picked in the shell's panel. */
static void
project_take(ide_t *st, const char *path)
{
    static char whole[64 * 1024];
    FILE *f = fopen(path, "r");
    char dir[512];
    char *slash;
    size_t got;

    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "cannot read %s", path);
        project_paint(st);
        return;
    }
    got = fread(whole, 1u, sizeof whole - 1u, f);
    whole[got] = '\0';
    (void)fclose(f);
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    (void)project_parse(&st->proj, whole, dir);
    project_show(st);
}

/*---------------------------------------------------------------------------*/
/* The application                                                           */
/*---------------------------------------------------------------------------*/

static int
ide_start(void **state, const tiku_app_services_t *services)
{
    ide_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->proj_surface = tiku_surface_new(IDE_W, IDE_H, TIKU_C_PANEL);
    if (st->proj_surface == NULL) {
        free(st);
        return -1;
    }
    tiku_list_init(&st->tv_list, 1);
    st->proj_id = services->open(services->ctx, "Project", IDE_W, IDE_H);
    project_paint(st);
    project_publish(st);
    *state = st;
    return 0;
}

static void
ide_stop(void *state)
{
    ide_t *st = state;
    int i;

    if (st == NULL) {
        return;
    }
    for (i = 0; i < IDE_EDITS; i++) {
        tiku_surface_free(st->edit[i].surface);
    }
    tiku_surface_free(st->proj_surface);
    free(st);
}

/** @brief Open whatever row the list is standing on. */
static void
open_chosen(ide_t *st)
{
    int row = tiku_list_chosen(&st->tv_list);

    if (row >= 0 && row < st->proj.nrow &&
        !st->proj.row[row].heading) {
        edit_open(st, &st->proj.row[row]);
    }
}

static int
ide_event(void *state, uint32_t window, const tiku_event_t *event)
{
    ide_t *st = state;
    edit_t *e = edit_of_id(st, window);

    if (event->type != TIKU_EVENT_KEY_DOWN &&
        event->type != TIKU_EVENT_POINTER_DOWN) {
        return 0;
    }
    if (e != NULL) {
        /* An editor window: the document takes it. */
        unsigned ch = (unsigned char)event->text[0];

        if (event->type == TIKU_EVENT_POINTER_DOWN) {
            return 0;
        }
        if (ch == 0u) {
            ch = event->key;
        }
        if (event->key == TIKU_KEY_RETURN) {
            tiku_textview_newline(&e->tv);
        } else if (event->key == TIKU_KEY_BACKSPACE) {
            tiku_textview_backspace(&e->tv);
        } else if (ch >= 32u && ch < 127u) {
            tiku_textview_insert(&e->tv, (char)ch);
        } else {
            return 0;
        }
        edit_paint(st, e);
        edit_publish(st, e);
        project_publish(st);    /* the mark follows the document */
        return 0;
    }
    if (window != st->proj_id) {
        return 0;
    }
    if (event->type == TIKU_EVENT_POINTER_DOWN) {
        tiku_list_click(&st->tv_list,
                        tiku_list_at(&st->tv_list, project_body(),
                                     event->x, event->y),
                        event->modifiers);
        project_paint(st);
        return 0;
    }
    if (event->key == TIKU_KEY_RETURN) {
        /* What opening a row MEANS is the application's, which is why
         * the list hands Return back rather than taking it. */
        open_chosen(st);
        return 0;
    }
    if (tiku_list_key(&st->tv_list, event->key, event->modifiers,
                      project_body())) {
        project_paint(st);
    }
    return 0;
}

static int
ide_pick(void *state, uint32_t window, int command)
{
    ide_t *st = state;
    edit_t *e = edit_of_id(st, window);

    if (command >= CMD_WINDOW) {
        int i = command - CMD_WINDOW;

        if (i >= 0 && i < IDE_EDITS && st->edit[i].surface != NULL &&
            st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             st->edit[i].id);
        }
        return 0;
    }
    switch (command) {
    case CMD_OPEN:
        if (st->services->pick != NULL) {
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_OPEN, NULL, NULL);
        }
        break;
    case CMD_SAVE:
        if (e != NULL) {
            (void)edit_save(e);
            edit_paint(st, e);
            edit_publish(st, e);
            project_publish(st);
        }
        break;
    case CMD_QUIT:
        if (e != NULL) {
            edit_drop(st, e);
        }
        break;
    case CMD_SHUT:
        return 1;               /* the project, and everything in it */
    default:
        break;
    }
    return 0;
}

static void
ide_picked(void *state, uint32_t window, const char *path)
{
    ide_t *st = state;

    (void)window;
    if (path != NULL) {
        project_take(st, path);
    }
}

/** @brief The runtime took a window; the project keeps the rest. */
static void
ide_closed(void *state, uint32_t window)
{
    ide_t *st = state;
    edit_t *e = edit_of_id(st, window);

    if (e != NULL) {
        tiku_surface_free(e->surface);
        e->surface = NULL;
        project_paint(st);
        project_publish(st);
    }
}

const tiku_app_descriptor_t tiku_ide_app = {
    .id = "org.tikuos.ide",
    .name = "TikuIDE",
    .start = ide_start,
    .stop = ide_stop,
    .pick = ide_pick,
    .picked = ide_picked,
    .event_in = ide_event,
    .closed = ide_closed
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_ide_app
};
#endif

#ifndef TIKU_APP_EMBED
int
main(void)
{
    return tiku_client_run(&tiku_ide_app);
}
#endif
