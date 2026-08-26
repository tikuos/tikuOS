/*
 * TikuDesktop -- graphical interface to TikuOS devices.
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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tiku_app.h"
#include "tiku_textview.h"
#include "tiku_alert.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_dl.h"
#include "tiku_gfx.h"
#include "tiku_syntax.h"
#include "tiku_ui.h"

#define EDIT_W      560
#define EDIT_H      400
#define STRIP_H     20
#define MARGIN      6
#define LINE_MAX    1024

#define CMD_SAVE   1
#define CMD_REVERT 2
#define CMD_QUIT   3
#define CMD_SYNTAX 4
#define CMD_OPEN   5
#define CMD_SAVEAS 6
#define CMD_RUN    7

typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    /*
     * What the painting below wrote down as it went.  The editor does not
     * paint twice for it: the surface records while it is drawn into, so
     * there is one painting path and a stream cannot come to disagree
     * with the picture.  NULL when the services this is running against
     * have no use for one -- linked into a desktop, they do not.
     */
    tiku_dl_t                      *dl;
    uint32_t                        id;
    /* The text, the caret and the scroll are the kit's now: one
     * document object every application that edits text can hold, rather
     * than each one growing its own line array and its own caret rules. */
    tiku_textview_t                 tv;
    /* The question the close gesture asks while there are unsaved
     * changes.  The kit's own alert, held over this window's content:
     * an application no longer needs a shell to be able to ask. */
    tiku_alert_t                    ask;
    tiku_rect_t                     ask_frame;
    int                             saved_shown;
    char                            path[512];
    char                            note[128];
    /*
     * Which language the document is written in, or none.  Taken from
     * the file's name when it is opened and offered on the menu after
     * that, because the name is the only thing that can honestly say --
     * and because an editor started with nothing (which is how the
     * desktop starts this one) has no name to go on and the person does.
     */
    tiku_syntax_lang_t              lang;
    /* Which question the shell's panel is answering, when it answers:
     * the ask is not remembered by the panel and the answer arrives
     * later, so what to DO with a path is remembered here. */
    int                             pick_save;
    /*
     * What the last run said, and the run itself while it lives.  The
     * program runs in the interpreter a BOARD runs -- tiku-basic, the
     * kernel's own engine compiled for the host -- as a child on the
     * far end of a pipe, the way the terminal next door holds its
     * shell.  The pane exists while there are lines to show.
     */
    char                            out[48][96];
    int                             out_count;
    int                             running;
    int                             child_fd;
    pid_t                           child;
    char                            out_partial[96];
} edit_state_t;

/* The file named on the command line, before any state exists. */
static char edit_path[512];

static int
out_pane_h(const edit_state_t *st)
{
    const tiku_font_t *f = tiku_font_plain();

    if (st == NULL || (st->out_count == 0 && !st->running)) {
        return 0;               /* no run yet: the page is all editor */
    }
    return 7 * (f->height + 2) + 8;
}

static int
rows_visible_of(const edit_state_t *st)
{
    return (EDIT_H - STRIP_H - MARGIN - out_pane_h(st)) /
           (tiku_font_plain()->height + 2);
}

/** @brief Read @p path into the buffer.  A missing file is an empty one. */
static void
load(edit_state_t *st)
{
    FILE *f = fopen(st->path, "r");
    char buf[LINE_MAX];

    if (f != NULL) {
        /* Read whole, then handed over in one piece: the splitting into
         * lines is the document's own business now. */
        static char whole[TIKU_TEXTVIEW_LINES_MAX * 8];
        size_t got = fread(whole, 1u, sizeof whole - 1u, f);

        whole[got] = '\0';
        (void)fclose(f);
        tiku_textview_set(&st->tv, whole);
        snprintf(st->note, sizeof st->note, "%d line%s",
                 tiku_textview_lines(&st->tv),
                 tiku_textview_lines(&st->tv) == 1 ? "" : "s");
    } else {
        tiku_textview_set(&st->tv, "");
        snprintf(st->note, sizeof st->note, "new file");
    }
    (void)buf;
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
    for (i = 0; i < tiku_textview_lines(&st->tv); i++) {
        (void)fprintf(f, "%s\n", tiku_textview_line(&st->tv, i));
    }
    /* The close is what can still fail -- a full disk reports here and
     * nowhere earlier -- so the saved state waits for it. */
    if (fclose(f) != 0) {
        snprintf(st->note, sizeof st->note, "could not finish writing");
        return 0;
    }
    tiku_textview_saved(&st->tv);
    snprintf(st->note, sizeof st->note, "saved %d line%s",
             tiku_textview_lines(&st->tv),
             tiku_textview_lines(&st->tv) == 1 ? "" : "s");
    return 1;
}

static void
paint(edit_state_t *st)
{
    const tiku_font_t *f = tiku_font_plain();
    const tiku_font_t *small = tiku_font_at(11);
    int step = f->height + 2;
    int rows = rows_visible_of(st);
    int pane_h = out_pane_h(st);

    /* Painting and recording are the same pass; the list is emptied
     * first because a frame is the whole window, not the difference. */
    if (st->dl != NULL) {
        tiku_dl_clear(st->dl);
    }
    st->surface->record = st->dl;
    tiku_rect_t page = { 0, 0, EDIT_W, EDIT_H - STRIP_H - pane_h };
    tiku_rect_t pane = { 0, EDIT_H - STRIP_H - pane_h, EDIT_W, pane_h };
    tiku_rect_t strip = { 0, EDIT_H - STRIP_H, EDIT_W, STRIP_H };
    char where[128];
    int i;

    tiku_fill(st->surface, page, TIKU_C_DOC);
    for (i = 0; i < rows &&
             tiku_textview_top(&st->tv) + i < tiku_textview_lines(&st->tv);
         i++) {
        int y = MARGIN + i * step;

        const char *text = tiku_textview_line(&st->tv,
                               tiku_textview_top(&st->tv) + i);

        if (st->lang != TIKU_SYNTAX_NONE) {
            /* Classified as it is painted, per visible line: nothing is
             * kept, so nothing can fall out of step with the text.  An
             * edit changes what the next frame paints and there is no
             * second copy to remember to update. */
            tiku_span_t span[64];
            int n = tiku_syntax_spans(st->lang, text, span,
                                      (int)(sizeof span / sizeof span[0]));

            (void)tiku_ui_text_spans(st->surface, f, MARGIN, y + f->ascent,
                                     text, span, n);
        } else {
            tiku_text(st->surface, f, MARGIN, y + f->ascent, text,
                           TIKU_C_TEXT);
        }
    }
    {
        /* The caret sits where the text before it ends, which is the only
         * way a proportional face can place it. */
        int cy, cx, row;
        char head[LINE_MAX];
        int cut;

        tiku_textview_caret(&st->tv, &cy, &cx);
        row = cy - tiku_textview_top(&st->tv);
        cut = cx;
        if (row >= 0 && row < rows) {
            if (cut > tiku_textview_line_len(&st->tv, cy)) {
                cut = tiku_textview_line_len(&st->tv, cy);
            }
            memcpy(head, tiku_textview_line(&st->tv, cy), (size_t)cut);
            head[cut] = '\0';
            tiku_vline(st->surface,
                            MARGIN + tiku_text_width(f, head),
                            MARGIN + row * step, step, TIKU_C_TEXT);
        }
    }
    if (pane_h > 0) {
        /* What the run said, newest lines last: a well of its own, with
         * the words in the OUTPUT ink so a reader is told these came
         * from the program rather than being part of it. */
        int fit = (pane.h - 8) / step;
        int first = (st->out_count > fit) ? st->out_count - fit : 0;
        int k;

        tiku_ui_sunken(st->surface, pane, TIKU_C_DOC);
        for (k = first; k < st->out_count; k++) {
            tiku_text(st->surface, f, pane.x + MARGIN,
                           pane.y + 4 + (k - first) * step + f->ascent,
                           st->out[k], tiku_ink(TIKU_INK_REMARK));
        }
        if (st->running && st->out_count == 0) {
            tiku_text(st->surface, f, pane.x + MARGIN,
                           pane.y + 4 + f->ascent, "running…",
                           tiku_dim(TIKU_C_TEXT, TIKU_C_DOC));
        }
    }
    tiku_fill(st->surface, strip, TIKU_C_PANEL);
    tiku_hline(st->surface, 0, strip.y, EDIT_W,
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    /* Marked in ASCII: the interface face carries no bullet, and a
     * modified file that says nothing about it is the worst of the
     * three states this line can be in. */
    {
        int cy, cx;

        tiku_textview_caret(&st->tv, &cy, &cx);
        snprintf(where, sizeof where, "%s%s  %d:%d  %s",
                 tiku_textview_modified(&st->tv) ? "* " : "",
                 st->path[0] != '\0' ? st->path : "untitled",
                 cy + 1, cx + 1, st->note);
    }
    tiku_text(st->surface, small, MARGIN,
                   strip.y + (STRIP_H - small->height) / 2 + small->ascent,
                   where, TIKU_C_TEXT);
    if (st->ask.open) {
        int aw, ah;

        tiku_alert_size(&aw, &ah);
        if (aw > EDIT_W - 16) { aw = EDIT_W - 16; }
        st->ask_frame = (tiku_rect_t){ (EDIT_W - aw) / 2,
                                       (EDIT_H - ah) / 3, aw, ah };
        tiku_alert_draw(&st->ask, st->surface, st->ask_frame);
    }
    st->surface->record = NULL;
    /*
     * Handed over BOTH ways, and the services layer picks: down a wire
     * the commands go, in one process the pixels do, and this does not
     * have to know which -- which is the same bargain the window session
     * already makes about where the application is running.
     */
    if (st->services->present != NULL) {
        (void)st->services->present(st->services->ctx, st->id, st->dl,
                                    st->surface->px, EDIT_W, EDIT_H);
    } else {
        (void)st->services->frame(st->services->ctx, st->id,
                                  st->surface->px, EDIT_W, EDIT_H);
    }
}

static void paint(edit_state_t *st);

/** @brief Append one finished line of the child's output to the pane. */
static void
out_line(edit_state_t *st, const char *line)
{
    if (st->out_count >= (int)(sizeof st->out / sizeof st->out[0])) {
        /* Full: the oldest line goes, because the newest is the one a
         * person watching a run is waiting for. */
        memmove(st->out[0], st->out[1],
                sizeof st->out - sizeof st->out[0]);
        st->out_count--;
    }
    snprintf(st->out[st->out_count], sizeof st->out[0], "%s", line);
    st->out_count++;
}

/**
 * @brief Run the document in the interpreter a board runs.
 *
 * The text is written to its file first (or a scratch one when it has
 * no name yet), because the interpreter reads a FILE -- what runs is
 * what would run tomorrow, not a copy that only ever existed in this
 * window.  The sandbox the program's VFS lands in is the file's own
 * folder: a program that writes /led writes beside its source, where
 * its author can look at it.
 */
static void
run_program(edit_state_t *st)
{
    char file[560];
    char box[560];
    const char *tool;
    int fds[2];
    const char *slash;

    if (st->running) {
        snprintf(st->note, sizeof st->note, "already running");
        return;
    }
    if (st->path[0] != '\0') {
        (void)save(st);
        snprintf(file, sizeof file, "%s", st->path);
    } else {
        FILE *f;
        int i;

        snprintf(file, sizeof file, "/tmp/tiku_edit_run.bas");
        f = fopen(file, "w");
        if (f == NULL) {
            snprintf(st->note, sizeof st->note, "cannot write the scratch");
            return;
        }
        for (i = 0; i < tiku_textview_lines(&st->tv); i++) {
            (void)fprintf(f, "%s\n", tiku_textview_line(&st->tv, i));
        }
        (void)fclose(f);
    }
    snprintf(box, sizeof box, "%s", file);
    slash = strrchr(box, '/');
    if (slash != NULL && slash != box) {
        box[slash - box] = '\0';
    } else {
        snprintf(box, sizeof box, "/tmp");
    }
    tool = getenv("TIKU_BASIC");
    if (tool == NULL || tool[0] == '\0') {
        tool = "tiku-basic";
    }
    if (pipe(fds) != 0) {
        snprintf(st->note, sizeof st->note, "cannot make the pipe");
        return;
    }
    st->child = fork();
    if (st->child < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        snprintf(st->note, sizeof st->note, "cannot start the run");
        return;
    }
    if (st->child == 0) {
        (void)dup2(fds[1], 1);
        (void)dup2(fds[1], 2);
        (void)close(fds[0]);
        (void)close(fds[1]);
        (void)execlp(tool, tool, file, box, (char *)NULL);
        /* Reached only when the interpreter is not there to run. */
        fprintf(stderr, "cannot run %s\n", tool);
        _exit(127);
    }
    (void)close(fds[1]);
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    st->child_fd = fds[0];
    st->running = 1;
    st->out_count = 0;
    st->out_partial[0] = '\0';
    snprintf(st->note, sizeof st->note, "running");
}

/** @brief Hear what the run has said since the last frame. */
static void
edit_tick(void *state, int64_t now_us)
{
    edit_state_t *st = state;
    char buf[256];
    ssize_t n;
    int heard = 0;

    (void)now_us;
    if (st == NULL || !st->running) {
        return;
    }
    for (;;) {
        n = read(st->child_fd, buf, sizeof buf - 1u);
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';
        {
            char *p = buf;

            while (*p != '\0') {
                size_t used = strlen(st->out_partial);

                if (*p == '\n') {
                    out_line(st, st->out_partial);
                    st->out_partial[0] = '\0';
                } else if (used + 1u < sizeof st->out_partial) {
                    st->out_partial[used] = *p;
                    st->out_partial[used + 1u] = '\0';
                }
                p++;
            }
        }
        heard = 1;
    }
    if (n == 0) {
        /* The pipe closed: the run is over, and its verdict is worth a
         * line of its own. */
        int status = 0;

        if (st->out_partial[0] != '\0') {
            out_line(st, st->out_partial);
            st->out_partial[0] = '\0';
        }
        (void)waitpid(st->child, &status, 0);
        st->running = 0;
        (void)close(st->child_fd);
        st->child_fd = -1;
        snprintf(st->note, sizeof st->note, "%s",
                 (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                     ? "finished" : "stopped");
        heard = 1;
    }
    if (heard) {
        paint(st);
    }
}

static void
publish(edit_state_t *st)
{
    tiku_menuset_t menus;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "File");
    menus.menu[0].nitem = 4;
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "Save");
    menus.menu[0].item[0].command = CMD_SAVE;
    menus.menu[0].item[0].sc = 's';
    /* Offered disabled with nothing to save, so the row keeps its place
     * and its shortcut stays discoverable. */
    menus.menu[0].item[0].enabled =
        (unsigned char)tiku_textview_modified(&st->tv);
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Revert");
    menus.menu[0].item[1].command = CMD_REVERT;
    menus.menu[0].item[1].enabled =
        (unsigned char)tiku_textview_modified(&st->tv);
    snprintf(menus.menu[0].item[2].label,
             sizeof menus.menu[0].item[2].label, "Quit");
    menus.menu[0].item[2].command = CMD_QUIT;
    menus.menu[0].item[2].enabled = 1;
    /*
     * A settled row, marked when it is on.  The published protocol does
     * carry a mark and the bar draws it, so the row keeps ONE name and
     * says whether it holds -- rather than renaming itself, which reads
     * as two different commands to anyone scanning the menu twice.
     */
    snprintf(menus.menu[0].item[3].label,
             sizeof menus.menu[0].item[3].label, "Colour as BASIC");
    menus.menu[0].item[3].command = CMD_SYNTAX;
    menus.menu[0].item[3].enabled = 1;
    menus.menu[0].item[3].marked =
        (unsigned char)(st->lang != TIKU_SYNTAX_NONE);
    /*
     * Opening and saving-as are the SHELL's panel, offered only where
     * the host has one: an application started by a host that cannot
     * ask has no business showing rows it could not honour.
     */
    /* Run is offered where it can be honest: a document the editor
     * knows is BASIC, run by the interpreter a board runs. */
    menus.menu[0].nitem = 5;
    snprintf(menus.menu[0].item[4].label,
             sizeof menus.menu[0].item[4].label, "Run");
    menus.menu[0].item[4].command = CMD_RUN;
    menus.menu[0].item[4].sc = 'r';
    menus.menu[0].item[4].enabled =
        (unsigned char)(st->lang == TIKU_SYNTAX_BASIC && !st->running);
    if (st->services->pick != NULL) {
        menus.menu[0].nitem = 7;
        snprintf(menus.menu[0].item[5].label,
                 sizeof menus.menu[0].item[5].label, "Open…");
        menus.menu[0].item[5].command = CMD_OPEN;
        menus.menu[0].item[5].enabled = 1;
        snprintf(menus.menu[0].item[6].label,
                 sizeof menus.menu[0].item[6].label, "Save as…");
        menus.menu[0].item[6].command = CMD_SAVEAS;
        menus.menu[0].item[6].enabled = 1;
    }
    (void)st->services->menus(st->services->ctx, st->id, &menus);
}

/**
 * @brief Note that an edit happened: the menu may have to change.
 *
 * The document owns the modified flag; what this owns is the moment it
 * FLIPS, because that is when Save and Revert become reachable and the
 * menus have to be published again.
 */
static void
touched(edit_state_t *st, int was)
{
    st->note[0] = '\0';
    if (!was && tiku_textview_modified(&st->tv)) {
        publish(st);            /* Save and Revert become reachable */
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
    tiku_textview_init(&st->tv);
    st->surface = tiku_surface_new(EDIT_W, EDIT_H, TIKU_C_DOC);
    /* Only worth keeping when something can use it; a NULL one simply
     * means every frame goes over as pixels, which is what happens
     * linked into a desktop anyway. */
    st->dl = (services->present != NULL) ? tiku_dl_new() : NULL;
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    snprintf(st->path, sizeof st->path, "%s", edit_path);
    /* The name decides, once, before anything is drawn; after that the
     * menu does.  A file called .bas opens coloured, and everything else
     * opens as what it is -- prose. */
    st->lang = tiku_syntax_of_path(st->path);
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
        tiku_textview_free(&st->tv);
        tiku_dl_free(st->dl);
        tiku_surface_free(st->surface);
        free(st);
    }
}

static int
edit_event(void *state, const tiku_event_t *event)
{
    edit_state_t *st = state;
    int rows = rows_visible_of(st);
    int was_modified;

    if (st->ask.open) {
        int at = -1;

        if (event->type == TIKU_EVENT_KEY_DOWN) {
            at = tiku_alert_key(&st->ask, event->key);
        } else if (event->type == TIKU_EVENT_POINTER_DOWN) {
            at = tiku_alert_button_at(&st->ask,
                     st->ask_frame.w, st->ask_frame.h,
                     event->x - st->ask_frame.x,
                     event->y - st->ask_frame.y);
            if (at >= 0) {
                st->ask.chosen = at;
            }
        } else {
            return 0;
        }
        if (at < 0) {
            return 0;           /* not the question's key: ignored */
        }
        tiku_alert_reset(&st->ask);
        /* Right to left: Cancel(0) stays, Discard(1) goes.  A save road
         * would go here the day the editor grows one for a file it can
         * name; the untitled one it is launched as has nowhere to save. */
        if (at == 1) {
            return 1;           /* done: the host closes the window */
        }
        paint(st);
        return 0;
    }
    if (event->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    was_modified = tiku_textview_modified(&st->tv);
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
        if (tiku_textview_modified(&st->tv)) {
            /* The question, not the guillotine: closing must not be the
             * one gesture that silently destroys typing. */
            tiku_alert_open(&st->ask, TIKU_ALERT_WARN, 0,
                            "Discard unsaved changes? "
                            "The window has changes nothing has saved.",
                            "Cancel|Discard");
            paint(st);
            return 0;
        }
        return 1;
    case TIKU_KEY_RETURN:
        tiku_textview_newline(&st->tv);
        break;
    case TIKU_KEY_BACKSPACE:
        tiku_textview_backspace(&st->tv);
        break;
    case TIKU_KEY_DELETE:
        tiku_textview_delete(&st->tv);
        break;
    case TIKU_KEY_LEFT:
        tiku_textview_move(&st->tv, TIKU_TEXTVIEW_LEFT);
        break;
    case TIKU_KEY_RIGHT:
        tiku_textview_move(&st->tv, TIKU_TEXTVIEW_RIGHT);
        break;
    case TIKU_KEY_UP:
        tiku_textview_move(&st->tv, TIKU_TEXTVIEW_UP);
        break;
    case TIKU_KEY_DOWN:
        tiku_textview_move(&st->tv, TIKU_TEXTVIEW_DOWN);
        break;
    case TIKU_KEY_HOME:
        tiku_textview_move(&st->tv, TIKU_TEXTVIEW_HOME);
        break;
    case TIKU_KEY_END:
        tiku_textview_move(&st->tv, TIKU_TEXTVIEW_END);
        break;
    case TIKU_KEY_PAGE_UP:
    case TIKU_KEY_PAGE_DOWN: {
        /* A page is a screenful of lines, and the caret keeps its column
         * where the line it lands on is long enough to hold it. */
        int cy, cx;

        tiku_textview_caret(&st->tv, &cy, &cx);
        cy += (event->key == TIKU_KEY_PAGE_UP) ? -rows : rows;
        tiku_textview_place(&st->tv, cy, cx);
        break;
    }
    default:
        if (event->key >= 32u && event->key < 127u) {
            tiku_textview_insert(&st->tv, (char)event->key);
        } else {
            return 0;           /* not ours: no repaint owed */
        }
        break;
    }
    touched(st, was_modified);
    (void)tiku_textview_reveal(&st->tv, rows);
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
    case CMD_RUN:
        if (st->lang == TIKU_SYNTAX_BASIC) {
            run_program(st);
        }
        break;
    case CMD_OPEN:
    case CMD_SAVEAS:
        if (st->services->pick != NULL) {
            char dir[512];
            const char *slash;

            /* Start where this document lives, or at the home the shell
             * would have chosen anyway. */
            snprintf(dir, sizeof dir, "%s", st->path);
            slash = strrchr(dir, '/');
            if (slash != NULL && slash != dir) {
                dir[slash - dir] = '\0';
            } else {
                dir[0] = '\0';
            }
            st->pick_save = (command == CMD_SAVEAS);
            (void)st->services->pick(st->services->ctx, st->id,
                st->pick_save ? TIKU_APP_PICK_SAVE : TIKU_APP_PICK_OPEN,
                (dir[0] != '\0') ? dir : NULL,
                st->pick_save ? "untitled" : NULL);
        }
        break;
    case CMD_SYNTAX:
        st->lang = (st->lang == TIKU_SYNTAX_NONE) ? TIKU_SYNTAX_BASIC
                                                  : TIKU_SYNTAX_NONE;
        snprintf(st->note, sizeof st->note, "%s",
                 (st->lang == TIKU_SYNTAX_NONE) ? "plain text"
                                                : "coloured as BASIC");
        break;
    default:
        return 0;
    }
    publish(st);
    paint(st);
    return 0;
}

/**
 * @brief The path the person chose, some frames after the asking.
 *
 * Opening reads the file and re-reads its NAME for the language, so a
 * .bas file arrives coloured without anybody asking twice.  Saving-as
 * takes the name first and then writes, so a failed write leaves the
 * document pointing where it was told rather than where it was.
 */
static void
edit_picked(void *state, uint32_t window, const char *path)
{
    edit_state_t *st = state;

    (void)window;
    if (st == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    snprintf(st->path, sizeof st->path, "%s", path);
    st->lang = tiku_syntax_of_path(st->path);
    if (st->pick_save) {
        (void)save(st);
    } else {
        load(st);
    }
    publish(st);
    paint(st);
}

const tiku_app_descriptor_t tiku_edit_app = {
    .id = "org.tikuos.edit",
    .name = "Edit",
    .start = edit_start,
    .stop = edit_stop,
    .event = edit_event,
    .pick = edit_pick,
    .tick = edit_tick,
    .picked = edit_picked
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
