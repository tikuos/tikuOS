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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tiku_alert.h"
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

#define MSG_W      420
#define MSG_H      220

#define IDE_ROWS   64
/* A session may hold four windows over the wire, and the project and
 * the messages are two of them. */
#define IDE_EDITS  2
#define IDE_MSGS   200
/* How long a run may take before it is stopped rather than trusted. */
#define IDE_RUN_LIMIT_US 30000000

#define CMD_OPEN     1
#define CMD_QUIT     2
#define CMD_SAVE     3
#define CMD_SHUT     4
#define CMD_RUN      5
#define CMD_FOLLOW   6
#define CMD_BUILD    7
#define CMD_NEWPROJ  8
#define CMD_NEWFILE  9
#define CMD_ADDFILE  10

/* Which question the shell's panel is answering, when it answers. */
enum {
    ASK_OPEN = 0,               /* Open Project…: the path IS a project */
    ASK_NEWPROJ,                /* New Project…: make it, then open it  */
    ASK_FILE                    /* New/Add File…: put it in the project */
};
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
    char  target[32];           /* what the program is being built FOR */
    int   nrow;
    int   unknown;              /* lines this build did not know */
    prow_t row[IDE_ROWS];
} project_t;

/**
 * @brief The compiler a target is built with.
 *
 * The names the tree's own Makefile already detects, so a project says
 * what it is FOR and the toolchain follows from that rather than from
 * a path somebody typed.  A target this build has never heard of gets
 * no compiler and is told so; a compiler that is not installed says so
 * itself, through the same road every other failure takes.
 */
static const char *
compiler_for(const char *target)
{
    static const struct { const char *target, *cc; } WHICH[] = {
        { "host",      "cc"                 },
        { "rp2350",    "arm-none-eabi-gcc"  },
        { "ambiq",     "arm-none-eabi-gcc"  },
        { "nordic",    "arm-none-eabi-gcc"  },
        { "stm32n6",   "arm-none-eabi-gcc"  },
        { "ra8p1",     "arm-none-eabi-gcc"  },
        { "msp430",    "msp430-elf-gcc"     }
    };
    size_t i;

    for (i = 0; i < sizeof WHICH / sizeof WHICH[0]; i++) {
        if (strcmp(target, WHICH[i].target) == 0) {
            return WHICH[i].cc;
        }
    }
    return NULL;
}

/** @brief One editor window: its file, its text, and its window. */
typedef struct {
    uint32_t             id;
    tiku_surface_t *surface;    /* NULL when the seat is free */
    tiku_textview_t tv;
    tiku_syntax_lang_t   lang;
    char                 path[512];
    char                 name[64];
} edit_t;

/**
 * @brief One thing a run said.
 *
 * The whole of what the IDE knows about a backend: words, and -- when
 * the words were about somewhere -- the file and the line they were
 * about.  Whatever runs a program later says the same three things or
 * it does not get a window.
 */
typedef struct {
    char text[160];
    char file[512];             /* empty when it names nowhere */
    int  line;                  /* the line, or 0 for none */
    /* WHAT the line counts.  A compiler counts the lines of the file; a
     * BASIC program's lines wear their own numbers and are not in file
     * order at all.  Getting this wrong lands the caret somewhere
     * plausible and wrong, which is worse than not moving. */
    int  numbered;              /* nonzero: the line is a BASIC number */
} msg_t;

typedef struct {
    const tiku_app_services_t *services;
    uint32_t                        proj_id;
    tiku_surface_t            *proj_surface;
    tiku_list_t                tv_list;
    project_t                       proj;
    edit_t                          edit[IDE_EDITS];
    /* The question closing asks while there is work nothing has saved.
     * ONE question for the whole project, naming what is at stake: a
     * question per window is a person pressing the same button three
     * times without reading it. */
    tiku_alert_t               ask;
    tiku_rect_t                ask_frame;
    char                            note[128];
    /* The project FILE itself, because adding to a project means
     * appending to the file that describes it -- appending, so the
     * lines this build does not know survive being edited by it. */
    char                            proj_file[512];
    int                             asked;      /* which question, ASK_* */
    /* The click before this one, because opening by mouse is two of
     * them: the same row, close together.  The host synthesises no
     * double-click event, and the row's own clock is the event's. */
    int                             click_row;
    int64_t                         click_at;
    /* What the last run said, and the window it says it in.  The
     * window arrives when there is something to put in it: a message
     * window standing empty is furniture. */
    uint32_t                        msg_id;
    tiku_surface_t            *msg_surface;
    tiku_list_t                msg_list;
    msg_t                           msg[IDE_MSGS];
    int                             nmsg;
    /* The run itself: a child on the far end of a pipe, pumped from
     * tick, which is called wherever this application is hosted. */
    int                             running;
    int                             child_fd;
    pid_t                           child;
    char                            child_file[512];
    char                            partial[160];
    int                             child_numbered; /* a BASIC run */
    int64_t                         run_from;   /* when it started */
} ide_t;

static void project_paint(ide_t *st);
static void project_publish(ide_t *st);
static void messages_show(ide_t *st);

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
        if (strcmp(kind, "target") == 0) {
            /* What the program is FOR.  It was an unknown line until
             * something could act on it, which is exactly what the
             * skipping rule is for. */
            (void)word(rest, pj->target, sizeof pj->target);
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
    return tiku_syntax_of_path(name);
}

static void
edit_paint(ide_t *st, edit_t *e)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t page = { 0, 0, EDIT_W, EDIT_H - STRIP_H };
    tiku_rect_t strip = { 0, EDIT_H - STRIP_H, EDIT_W, STRIP_H };
    int rows = (EDIT_H - STRIP_H - MARGIN) / (f->height + 2);
    tiku_syntax_state_t carry = TIKU_SYNTAX_OPEN;
    int top = tiku_textview_top(&e->tv);
    int i;
    char where[160];

    if (e->surface == NULL) {
        return;
    }
    /*
     * C carries a remark between lines, so the first line SHOWN cannot
     * be told apart on its own: the walk starts at the document's own
     * beginning, where the state is known, and comes forward.  Nothing
     * is cached, because a cache of this would have to be invalidated
     * by every edit and the arithmetic of that is worth more than the
     * walk -- a document is bounded, and a window is redrawn by hand.
     */
    if (e->lang == TIKU_SYNTAX_C) {
        tiku_span_t span[64];

        for (i = 0; i < top; i++) {
            const char *text = tiku_textview_line(&e->tv, i);

            if (text == NULL) {
                break;
            }
            (void)tiku_syntax_spans_on(e->lang, text, carry, &carry, span,
                                       (int)(sizeof span / sizeof span[0]));
        }
    }
    tiku_ui_sunken(e->surface, page, TIKU_C_DOC);
    for (i = 0; i < rows; i++) {
        int line = top + i;
        const char *text = tiku_textview_line(&e->tv, line);
        int y = MARGIN + i * (f->height + 2);

        if (text == NULL) {
            break;
        }
        if (e->lang != TIKU_SYNTAX_NONE) {
            tiku_span_t span[64];
            int n = tiku_syntax_spans_on(e->lang, text, carry, &carry,
                                      span,
                                      (int)(sizeof span / sizeof span[0]));

            (void)tiku_ui_text_spans(e->surface, f, MARGIN, y + f->ascent,
                                     text, span, n);
        } else {
            tiku_text(e->surface, f, MARGIN, y + f->ascent, text,
                           TIKU_C_TEXT);
        }
    }
    {
        /* The caret, without which a person cannot know where their
         * typing will land.  It sits where the text before it ends,
         * which is the only way a proportional face can place it. */
        int cy, cx, row;
        char head[256];
        int cut;

        tiku_textview_caret(&e->tv, &cy, &cx);
        row = cy - top;
        cut = cx;
        if (row >= 0 && row < rows) {
            const char *text = tiku_textview_line(&e->tv, cy);
            int len = tiku_textview_line_len(&e->tv, cy);

            if (cut > len) {
                cut = len;
            }
            if (cut > (int)sizeof head - 1) {
                cut = (int)sizeof head - 1;
            }
            memcpy(head, (text != NULL) ? text : "", (size_t)cut);
            head[cut] = '\0';
            tiku_vline(e->surface,
                            MARGIN + tiku_text_width(f, head),
                            MARGIN + row * (f->height + 2),
                            f->height + 2, TIKU_C_TEXT);
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
    } else if (st->proj_file[0] == '\0') {
        snprintf(says, sizeof says,
                 "no project open -- the Project menu starts one");
    } else {
        /* FILES, not rows: a group's name is furniture, and a project
         * holding one heading and nothing under it is empty. */
        int files = 0;
        int i;

        for (i = 0; i < st->proj.nrow; i++) {
            files += !st->proj.row[i].heading;
        }
        if (files == 0) {
            snprintf(says, sizeof says,
                     "%s: empty -- New File… adds one",
                     (st->proj.title[0] != '\0') ? st->proj.title
                                                 : "a project");
        } else {
            snprintf(says, sizeof says, "%d file%s", files,
                     files == 1 ? "" : "s");
        }
    }
    tiku_text(st->proj_surface, f, MARGIN,
                   strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                   says, TIKU_C_TEXT);
    if (st->ask.open) {
        int aw, ah;

        tiku_alert_size(&aw, &ah);
        if (aw > IDE_W - 16) {
            aw = IDE_W - 16;
        }
        st->ask_frame = (tiku_rect_t){ (IDE_W - aw) / 2,
                                            (IDE_H - ah) / 3, aw, ah };
        tiku_alert_draw(&st->ask, st->proj_surface, st->ask_frame);
    }
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
             sizeof menus.menu[0].item[0].label, "New Project…");
    menus.menu[0].item[0].command = CMD_NEWPROJ;
    menus.menu[0].item[0].enabled = 1;
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Open Project…");
    menus.menu[0].item[1].command = CMD_OPEN;
    menus.menu[0].item[1].enabled = 1;
    /* The two roads a file takes INTO a project: made new, or already
     * on disk and brought in.  Neither means anything without a project
     * for it to land in, and the rows say so by dimming. */
    snprintf(menus.menu[0].item[2].label,
             sizeof menus.menu[0].item[2].label, "New File…");
    menus.menu[0].item[2].command = CMD_NEWFILE;
    menus.menu[0].item[2].enabled =
        (unsigned char)(st->proj_file[0] != '\0');
    snprintf(menus.menu[0].item[3].label,
             sizeof menus.menu[0].item[3].label, "Add File…");
    menus.menu[0].item[3].command = CMD_ADDFILE;
    menus.menu[0].item[3].enabled =
        (unsigned char)(st->proj_file[0] != '\0');
    {
        /* Run is offered for the row the list is standing on, and only
         * when its NAME says the interpreter would know what to do
         * with it -- the same thing the editor's own Run asks. */
        int row = tiku_list_chosen(&st->tv_list);
        int can = (row >= 0 && row < st->proj.nrow &&
                   !st->proj.row[row].heading &&
                   lang_of(st->proj.row[row].name) == TIKU_SYNTAX_BASIC &&
                   !st->running);

        snprintf(menus.menu[0].item[4].label,
                 sizeof menus.menu[0].item[4].label, "Run%s%s",
                 (row >= 0 && row < st->proj.nrow &&
                  !st->proj.row[row].heading) ? " " : "",
                 (row >= 0 && row < st->proj.nrow &&
                  !st->proj.row[row].heading) ? st->proj.row[row].name
                                              : "");
        menus.menu[0].item[4].command = CMD_RUN;
        menus.menu[0].item[4].enabled = (unsigned char)can;
    }
    /* Build names what it is FOR, because that is the decision the
     * project already made and the one a person needs to see before
     * they trust what comes back. */
    snprintf(menus.menu[0].item[5].label,
             sizeof menus.menu[0].item[5].label, "Build for %s",
             (st->proj.target[0] != '\0') ? st->proj.target
                                          : "nothing yet");
    menus.menu[0].item[5].command = CMD_BUILD;
    menus.menu[0].item[5].enabled =
        (unsigned char)(st->proj.nrow > 0 && !st->running);
    snprintf(menus.menu[0].item[6].label,
             sizeof menus.menu[0].item[6].label, "Close Project");
    menus.menu[0].item[6].command = CMD_SHUT;
    menus.menu[0].item[6].enabled = 1;
    menus.menu[0].nitem = 7;

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
    snprintf(st->proj_file, sizeof st->proj_file, "%s", path);
    project_show(st);
}

/**
 * @brief Put @p path into the project, making the file when it is new.
 *
 * The description is APPENDED to, never rewritten: a rewrite from what
 * this build parsed would silently drop every line it did not know,
 * which is exactly the forward road the skipping rule keeps open.
 */
static void
project_add_file(ide_t *st, const char *path)
{
    size_t dirlen = strlen(st->proj.dir);
    const char *leaf;
    FILE *f;
    int i;

    if (st->proj_file[0] == '\0') {
        return;                 /* no project to put it in */
    }
    /* Inside the project's own folder, because the description names
     * its files RELATIVELY -- a project is a folder somebody can move,
     * and a file outside it would be left behind. */
    if (strncmp(path, st->proj.dir, dirlen) != 0 ||
        path[dirlen] != '/') {
        snprintf(st->note, sizeof st->note,
                 "a project's files live in its folder");
        project_paint(st);
        return;
    }
    leaf = path + dirlen + 1;
    for (i = 0; i < st->proj.nrow; i++) {
        if (!st->proj.row[i].heading &&
            strcmp(st->proj.row[i].path, path) == 0) {
            edit_open(st, &st->proj.row[i]);
            return;             /* already listed: adding means opening */
        }
    }
    f = fopen(path, "a");       /* made when new, kept when not */
    if (f != NULL) {
        (void)fclose(f);
    }
    f = fopen(st->proj_file, "a");
    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "cannot write the project");
        project_paint(st);
        return;
    }
    (void)fprintf(f, "file %s\n", leaf);
    (void)fclose(f);
    project_take(st, st->proj_file);
    for (i = 0; i < st->proj.nrow; i++) {
        if (!st->proj.row[i].heading &&
            strcmp(st->proj.row[i].path, path) == 0) {
            tiku_list_select_only(&st->tv_list, i);
            edit_open(st, &st->proj.row[i]);
            return;
        }
    }
}

/** @brief Make a project where the person said, and open it. */
static void
project_create(ide_t *st, const char *path)
{
    FILE *probe = fopen(path, "r");
    char dir[512];
    char *slash;

    if (probe != NULL) {
        /* Already there: making it again would empty it, and a person
         * naming an existing project almost always means to OPEN it. */
        (void)fclose(probe);
        project_take(st, path);
        return;
    }
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    {
        FILE *f = fopen(path, "w");
        const char *leaf = strrchr(dir, '/');

        if (f == NULL) {
            snprintf(st->note, sizeof st->note, "cannot write %s", path);
            project_paint(st);
            return;
        }
        /* Named for the folder that holds it, which is the one name
         * already chosen; a person who wants another edits the line. */
        (void)fprintf(f, "title %s\ngroup Sources\n",
                      (leaf != NULL) ? leaf + 1 : dir);
        (void)fclose(f);
    }
    project_take(st, path);
}

/*---------------------------------------------------------------------------*/
/* What a run said                                                           */
/*---------------------------------------------------------------------------*/

static const char *
msg_name(void *ctx, int row)
{
    ide_t *st = ctx;

    return (row >= 0 && row < st->nmsg) ? st->msg[row].text : NULL;
}

static void
messages_paint(ide_t *st)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t all = { 0, 0, MSG_W, MSG_H };
    tiku_rect_t strip = { 0, MSG_H - STRIP_H, MSG_W, STRIP_H };
    tiku_rect_t body = { MARGIN, MARGIN, MSG_W - 2 * MARGIN,
                              MSG_H - STRIP_H - 2 * MARGIN };
    char says[160];

    if (st->msg_surface == NULL) {
        return;
    }
    tiku_fill(st->msg_surface, all, TIKU_C_PANEL);
    tiku_ui_sunken(st->msg_surface, body, TIKU_C_DOC);
    tiku_list_draw(&st->msg_list, st->msg_surface, body, msg_name, st);
    tiku_fill(st->msg_surface, strip, TIKU_C_PANEL);
    snprintf(says, sizeof says, "%s",
             st->running ? "running…" : "the run has finished");
    tiku_text(st->msg_surface, f, MARGIN,
                   strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                   says, TIKU_C_TEXT);
    (void)st->services->frame(st->services->ctx, st->msg_id,
                              st->msg_surface->px, MSG_W, MSG_H);
}

/**
 * @brief The message window's menus.
 *
 * One row, and it is the gesture the window exists for: take me to
 * where this was about.
 */
static void
messages_publish(ide_t *st)
{
    tiku_menuset_t menus;
    int row = tiku_list_chosen(&st->msg_list);
    int links = (row >= 0 && row < st->nmsg && st->msg[row].line > 0);

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Message");
    menus.menu[0].nitem = 1;
    if (links) {
        snprintf(menus.menu[0].item[0].label,
                 sizeof menus.menu[0].item[0].label, "Go to line %d",
                 st->msg[row].line);
    } else {
        snprintf(menus.menu[0].item[0].label,
                 sizeof menus.menu[0].item[0].label,
                 "This one is not about anywhere");
    }
    menus.menu[0].item[0].command = CMD_FOLLOW;
    menus.menu[0].item[0].enabled = (unsigned char)links;
    (void)st->services->menus(st->services->ctx, st->msg_id, &menus);
}

/** @brief Make the message window when there is first something to say. */
static void
messages_open(ide_t *st)
{
    if (st->msg_surface != NULL) {
        return;
    }
    st->msg_surface = tiku_surface_new(MSG_W, MSG_H, TIKU_C_PANEL);
    if (st->msg_surface == NULL) {
        return;
    }
    st->msg_id = st->services->open(st->services->ctx, "Messages",
                                    MSG_W, MSG_H);
    if (st->msg_id == 0u) {
        tiku_surface_free(st->msg_surface);
        st->msg_surface = NULL;
        snprintf(st->note, sizeof st->note, "no room for the messages");
        project_paint(st);
        return;
    }
    tiku_list_init(&st->msg_list, 1);
}

/**
 * @brief Take one line a run said.
 *
 * The interpreter says where an error was on the line AFTER saying what
 * it was: "? syntax", then "at line 20".  The two are ONE message here,
 * because a person reading a list wants what happened and where in the
 * same row -- and because a row that is only a number is a row nobody
 * can act on.
 *
 * The annotation is folded ONLY onto a row that was an error, which is
 * the one place the interpreter puts it: a program that prints those
 * words itself keeps them.
 */
static void
message_take(ide_t *st, const char *line)
{
    unsigned n = 0u;
    msg_t *m;

    if (st->nmsg >= IDE_MSGS) {
        return;
    }
    /* The marker is not always at the head of the row: a PRINT that
     * ended in a semicolon leaves the line open, and the error is
     * written onto the end of it -- "value: ? division by zero". */
    if (sscanf(line, "at line %u", &n) == 1 && st->nmsg > 0 &&
        strstr(st->msg[st->nmsg - 1].text, "? ") != NULL) {
        m = &st->msg[st->nmsg - 1];
        m->line = (int)n;
        m->numbered = 1;
        snprintf(m->file, sizeof m->file, "%s", st->child_file);
        {
            char whole[160];

            snprintf(whole, sizeof whole, "%s, at line %u", m->text, n);
            snprintf(m->text, sizeof m->text, "%s", whole);
        }
        return;
    }
    m = &st->msg[st->nmsg++];
    memset(m, 0, sizeof *m);
    snprintf(m->text, sizeof m->text, "%s", line);
    if (st->child_numbered) {
        return;                 /* BASIC says where on its own line */
    }
    /*
     * A compiler says where at the HEAD of what it says --
     * "file.c:12:5: error: ..." -- so the place and the words arrive
     * together and there is nothing to fold.  The file is taken as
     * written: a compiler asked about a path answers about that path.
     */
    {
        const char *colon = strchr(line, ':');
        unsigned at = 0u;

        if (colon != NULL && colon != line &&
            sscanf(colon + 1, "%u", &at) == 1 && at > 0u) {
            size_t len = (size_t)(colon - line);

            if (len < sizeof m->file) {
                memcpy(m->file, line, len);
                m->file[len] = '\0';
                m->line = (int)at;
            }
        }
    }
}

/**
 * @brief Which text line holds the program's line @p want.
 *
 * A BASIC line WEARS its number, so the file itself says where the
 * interpreter meant -- there is no table to keep in step.
 *
 * @return the line, or -1 when the file has no such number.
 */
static int
line_of_number(const tiku_textview_t *tv, int want)
{
    int i, found = -1;

    for (i = 0; i < tiku_textview_lines(tv); i++) {
        const char *p = tiku_textview_line(tv, i);
        long got;
        char *end;

        if (p == NULL) {
            break;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        got = strtol(p, &end, 10);
        if (end != p && got == (long)want) {
            /* The LAST of them: storing a line REPLACES one already
             * numbered the same, so a file with the number twice ran
             * the second and the first was never in the program. */
            found = i;
        }
    }
    return found;
}

/**
 * @brief Run @p path on the interpreter a BOARD runs.
 *
 * The same engine, as a child on the far end of a pipe: what the
 * editor's Run does, for a file the project names.  The sandbox is the
 * file's own folder, so what a program writes lands beside its source.
 */
static void
run_file(ide_t *st, const char *path)
{
    char box[560];
    const char *tool;
    char *slash;
    int fds[2];

    if (st->running) {
        snprintf(st->note, sizeof st->note, "already running");
        project_paint(st);
        return;
    }
    snprintf(st->child_file, sizeof st->child_file, "%s", path);
    snprintf(box, sizeof box, "%s", path);
    slash = strrchr(box, '/');
    if (slash != NULL && slash != box) {
        *slash = '\0';
    } else {
        snprintf(box, sizeof box, "/tmp");
    }
    /* ABSOLUTE, because a runner starts an application in the person's
     * home rather than wherever the desktop was built. */
    tool = getenv("TIKU_BASIC");
    if (tool == NULL || tool[0] == '\0') {
        tool = "tiku-basic";
    }
    if (pipe(fds) != 0) {
        return;
    }
    st->child = fork();
    if (st->child < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return;
    }
    if (st->child == 0) {
        (void)dup2(fds[1], 1);
        (void)dup2(fds[1], 2);
        (void)close(fds[0]);
        (void)close(fds[1]);
        (void)execlp(tool, tool, path, box, (char *)NULL);
        /* A tool that is not installed says so through the same pipe
         * everything else does, and becomes a message like any other. */
        fprintf(stderr, "cannot run %s\n", tool);
        _exit(127);
    }
    st->child_numbered = 1;     /* BASIC names its own lines */
    (void)close(fds[1]);
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    st->child_fd = fds[0];
    st->running = 1;
    st->run_from = 0;           /* the first tick says when */
    st->nmsg = 0;
    st->partial[0] = '\0';
    /* No window yet: a run that says nothing has nothing to show, and
     * a message window standing empty is furniture. */
    (void)tiku_list_set_count(&st->msg_list, 0);
    messages_paint(st);
}

/**
 * @brief Ask the compiler the project's target names whether its C is
 *        good, and say whatever it says.
 *
 * What BUILD means, today: the check, not the image.  Making an image
 * for a board needs a toolchain for that board, and a project asking
 * for one this machine has not got is told so rather than shown a
 * silence it could mistake for success.
 */
static void
build_project(ide_t *st)
{
    const char *cc = compiler_for(st->proj.target);
    char inc[560];
    const char *argv[IDE_ROWS + 8];
    int argc = 0;
    int fds[2];
    int i, any = 0;

    if (st->running) {
        return;
    }
    st->nmsg = 0;
    st->partial[0] = '\0';
    st->child_numbered = 0;     /* a compiler counts the file's lines */
    (void)tiku_list_set_count(&st->msg_list, 0);
    if (st->proj.target[0] == '\0') {
        message_take(st, "? this project does not say what it is for");
        messages_show(st);
        return;
    }
    if (cc == NULL) {
        char says[160];

        snprintf(says, sizeof says,
                 "? nothing here knows how to build for %s",
                 st->proj.target);
        message_take(st, says);
        messages_show(st);
        return;
    }
    snprintf(inc, sizeof inc, "-I%s", st->proj.dir);
    argv[argc++] = cc;
    argv[argc++] = "-fsyntax-only";
    argv[argc++] = inc;
    for (i = 0; i < st->proj.nrow && argc < IDE_ROWS + 6; i++) {
        const prow_t *row = &st->proj.row[i];
        size_t n = strlen(row->name);

        if (row->heading || n < 3u ||
            strcmp(row->name + n - 2, ".c") != 0) {
            continue;
        }
        argv[argc++] = row->path;
        any = 1;
    }
    argv[argc] = NULL;
    if (!any) {
        message_take(st, "? this project has no C in it to build");
        messages_show(st);
        return;
    }
    if (pipe(fds) != 0) {
        return;
    }
    st->child = fork();
    if (st->child < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return;
    }
    if (st->child == 0) {
        (void)dup2(fds[1], 1);
        (void)dup2(fds[1], 2);
        (void)close(fds[0]);
        (void)close(fds[1]);
        (void)execvp(cc, (char *const *)argv);
        fprintf(stderr, "cannot run %s\n", cc);
        _exit(127);
    }
    (void)close(fds[1]);
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    st->child_fd = fds[0];
    st->running = 1;
    st->run_from = 0;
    st->child_file[0] = '\0';
    messages_paint(st);
}

/** @brief Open the file a message was about, at the line it named. */
static void
message_follow(ide_t *st)
{
    int row = tiku_list_chosen(&st->msg_list);
    const msg_t *m;
    edit_t *e;
    int at;

    if (row < 0 || row >= st->nmsg) {
        return;
    }
    m = &st->msg[row];
    if (m->line <= 0 || m->file[0] == '\0') {
        return;             /* a message about nowhere leads nowhere */
    }
    e = edit_of_path(st, m->file);
    if (e == NULL) {
        prow_t want;

        memset(&want, 0, sizeof want);
        snprintf(want.path, sizeof want.path, "%s", m->file);
        {
            const char *slash = strrchr(m->file, '/');

            snprintf(want.name, sizeof want.name, "%s",
                     (slash != NULL) ? slash + 1 : m->file);
        }
        edit_open(st, &want);
        e = edit_of_path(st, m->file);
    }
    if (e == NULL) {
        return;
    }
    /* A compiler counted the lines of the file; BASIC named one. */
    at = m->numbered ? line_of_number(&e->tv, m->line) : m->line - 1;
    if (at >= 0) {
        const tiku_font_t *f = tiku_font_plain();

        tiku_textview_place(&e->tv, at, 0);
        /* Placing the caret does not move the PAGE, and a caret below
         * the window is a jump that looks like nothing happening. */
        (void)tiku_textview_reveal(&e->tv,
            (EDIT_H - STRIP_H - MARGIN) / (f->height + 2));
        edit_paint(st, e);
    }
    if (st->services->raise_window != NULL) {
        (void)st->services->raise_window(st->services->ctx, e->id);
    }
}

static void
messages_show(ide_t *st)
{
    if (st->nmsg > 0) {
        messages_open(st);      /* now there is something to put in it */
    }
    if (st->msg_surface == NULL) {
        return;
    }
    (void)tiku_list_set_count(&st->msg_list, st->nmsg);
    messages_paint(st);
    messages_publish(st);
}

/*---------------------------------------------------------------------------*/
/* The application                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Pump the run.
 *
 * Called wherever this application is hosted -- linked in, or on the
 * far end of the desk socket -- which is what lets a project run a
 * program from a process of its own.
 */
static void
ide_tick(void *state, int64_t now_us)
{
    ide_t *st = state;
    char buf[256];
    ssize_t n = -1;
    int heard = 0;

    if (st == NULL || !st->running) {
        return;
    }
    if (st->run_from == 0) {
        st->run_from = now_us;
    }
    /*
     * BOUNDED, because a program is something somebody may have written
     * wrong: the interpreter stops its own endless loops, but a program
     * waiting for INPUT waits for a person who is not there, and an
     * application that sat on that would say "running…" until it was
     * killed.
     */
    if (now_us - st->run_from > IDE_RUN_LIMIT_US) {
        (void)kill(st->child, SIGTERM);
        (void)waitpid(st->child, NULL, 0);
        st->running = 0;
        (void)close(st->child_fd);
        st->child_fd = -1;
        message_take(st, "? the run was stopped: it did not finish");
        messages_show(st);
        return;
    }
    for (;;) {
        n = read(st->child_fd, buf, sizeof buf - 1u);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR) {
            n = 0;              /* a broken pipe is a finished run */
            break;
        }
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';
        {
            char *p = buf;

            while (*p != '\0') {
                size_t used = strlen(st->partial);

                if (*p == '\n') {
                    message_take(st, st->partial);
                    st->partial[0] = '\0';
                } else if (used + 1u < sizeof st->partial) {
                    st->partial[used] = *p;
                    st->partial[used + 1u] = '\0';
                }
                p++;
            }
        }
        heard = 1;
    }
    if (n == 0) {
        int status = 0;

        if (st->partial[0] != '\0') {
            message_take(st, st->partial);
            st->partial[0] = '\0';
        }
        (void)waitpid(st->child, &status, 0);
        st->running = 0;
        (void)close(st->child_fd);
        st->child_fd = -1;
        heard = 1;
    }
    if (heard) {
        messages_show(st);
    }
}

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
    st->click_row = -1;
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
    if (st->running) {
        /* A run outliving the application it was started from is a
         * program nobody is listening to. */
        (void)kill(st->child, SIGTERM);
        (void)waitpid(st->child, NULL, 0);
        (void)close(st->child_fd);
        st->running = 0;
    }
    for (i = 0; i < IDE_EDITS; i++) {
        tiku_surface_free(st->edit[i].surface);
    }
    tiku_surface_free(st->msg_surface);
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

/**
 * @brief Name what closing would throw away, or say there is nothing.
 *
 * @return the number of documents holding unsaved work.
 */
static int
unsaved(const ide_t *st, char *out, size_t max)
{
    int i, n = 0;
    size_t used = 0u;

    if (max > 0u) {
        out[0] = '\0';
    }
    for (i = 0; i < IDE_EDITS; i++) {
        const edit_t *e = &st->edit[i];

        if (e->surface == NULL || !tiku_textview_modified(&e->tv)) {
            continue;
        }
        used += (size_t)snprintf(out + used, (used < max) ? max - used : 0u,
                                 "%s%s", (n > 0) ? ", " : "", e->name);
        n++;
    }
    return n;
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
    if (st->ask.open && window == st->proj_id) {
        int at = -1;

        if (event->type == TIKU_EVENT_KEY_DOWN) {
            at = tiku_alert_key(&st->ask, event->key);
        } else {
            at = tiku_alert_button_at(&st->ask, st->ask_frame.w,
                     st->ask_frame.h, event->x - st->ask_frame.x,
                     event->y - st->ask_frame.y);
            if (at >= 0) {
                st->ask.chosen = at;
            }
        }
        if (at < 0) {
            return 0;           /* not the question's key: ignored */
        }
        tiku_alert_reset(&st->ask);
        if (at == 1) {
            return 1;           /* Discard: the project, and all of it */
        }
        project_paint(st);      /* Cancel: everything stands */
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
    if (window == st->msg_id && st->msg_surface != NULL) {
        tiku_rect_t body = { MARGIN, MARGIN, MSG_W - 2 * MARGIN,
                                  MSG_H - STRIP_H - 2 * MARGIN };

        if (event->type == TIKU_EVENT_POINTER_DOWN) {
            tiku_list_click(&st->msg_list,
                            tiku_list_at(&st->msg_list, body, event->x,
                                         event->y),
                            event->modifiers);
            messages_show(st);
            return 0;
        }
        if (event->key == TIKU_KEY_RETURN) {
            /* What FOLLOWING a message means is the application's, the
             * same way opening a row is. */
            message_follow(st);
            return 0;
        }
        if (tiku_list_key(&st->msg_list, event->key, event->modifiers,
                          body)) {
            messages_show(st);
        }
        return 0;
    }
    if (window != st->proj_id) {
        return 0;
    }
    if (event->type == TIKU_EVENT_POINTER_DOWN) {
        int row = tiku_list_at(&st->tv_list, project_body(), event->x,
                               event->y);

        /* The second click of two on the same row OPENS it, exactly as
         * Return does: a person with a mouse is owed the same door. */
        if (row >= 0 && row == st->click_row &&
            event->time_us - st->click_at < 400000 &&
            tiku_list_chosen(&st->tv_list) == row) {
            st->click_row = -1;
            open_chosen(st);
            return 0;
        }
        st->click_row = row;
        st->click_at = event->time_us;
        tiku_list_click(&st->tv_list, row, event->modifiers);
        project_paint(st);
        project_publish(st);    /* Run follows the row it would run */
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
        project_publish(st);    /* Run follows the row it would run */
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
            st->asked = ASK_OPEN;
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_OPEN, NULL, NULL);
        }
        break;
    case CMD_NEWPROJ:
        if (st->services->pick != NULL) {
            st->asked = ASK_NEWPROJ;
            /* The shell's own Save panel names the place and the file;
             * the offered name is the one the opener looks for. */
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_SAVE, NULL,
                                     "project.tiku");
        }
        break;
    case CMD_NEWFILE:
        if (st->services->pick != NULL && st->proj_file[0] != '\0') {
            st->asked = ASK_FILE;
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_SAVE, st->proj.dir,
                                     "untitled.bas");
        }
        break;
    case CMD_ADDFILE:
        if (st->services->pick != NULL && st->proj_file[0] != '\0') {
            st->asked = ASK_FILE;
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_OPEN, st->proj.dir,
                                     NULL);
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
    case CMD_RUN: {
        int row = tiku_list_chosen(&st->tv_list);

        if (row >= 0 && row < st->proj.nrow &&
            !st->proj.row[row].heading) {
            edit_t *open_now = edit_of_path(st, st->proj.row[row].path);

            /* What is RUN is what is on disk, so a window holding
             * changes is saved first -- running the last saved copy
             * while showing another is how a person debugs a program
             * they are not looking at. */
            if (open_now != NULL &&
                tiku_textview_modified(&open_now->tv)) {
                (void)edit_save(open_now);
                edit_paint(st, open_now);
                edit_publish(st, open_now);
            }
            run_file(st, st->proj.row[row].path);
            project_publish(st);
        }
        break;
    }
    case CMD_BUILD:
        build_project(st);
        project_publish(st);
        break;
    case CMD_FOLLOW:
        message_follow(st);
        break;
    case CMD_SHUT: {
        char names[192];

        if (unsaved(st, names, sizeof names) == 0) {
            return 1;           /* nothing at stake: closing is closing */
        }
        {
            char says[320];

            snprintf(says, sizeof says,
                     "Close the project? %s %s changes nothing has "
                     "saved.", names,
                     (strchr(names, ',') != NULL) ? "hold" : "holds");
            tiku_alert_open(&st->ask, TIKU_ALERT_WARN, 0, says,
                            "Cancel|Discard");
        }
        if (st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             st->proj_id);
        }
        project_paint(st);
        break;
    }
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
    if (path == NULL || path[0] == '\0') {
        return;
    }
    switch (st->asked) {
    case ASK_NEWPROJ:
        project_create(st, path);
        break;
    case ASK_FILE:
        project_add_file(st, path);
        break;
    default:
        project_take(st, path);
        break;
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
        return;
    }
    if (window == st->msg_id && st->msg_surface != NULL) {
        tiku_surface_free(st->msg_surface);
        st->msg_surface = NULL;
        st->msg_id = 0u;
    }
}

const tiku_app_descriptor_t tiku_ide_app = {
    .id = "org.tikuos.ide",
    .name = "TikuIDE",
    .start = ide_start,
    .stop = ide_stop,
    .pick = ide_pick,
    .picked = ide_picked,
    .tick = ide_tick,
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
