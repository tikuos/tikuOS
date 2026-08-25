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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_app.h"
#include "tiku_textview.h"
#include "tiku_alert.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_dl.h"
#include "tiku_gfx.h"

#define EDIT_W      560
#define EDIT_H      400
#define STRIP_H     20
#define MARGIN      6
#define LINE_MAX    1024

#define CMD_SAVE   1
#define CMD_REVERT 2
#define CMD_QUIT   3

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
} edit_state_t;

/* The file named on the command line, before any state exists. */
static char edit_path[512];

static int
rows_visible(void)
{
    const tiku_font_t *f = tiku_font_plain();

    return (EDIT_H - STRIP_H - MARGIN) / (f->height + 2);
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
    int rows = rows_visible();

    /* Painting and recording are the same pass; the list is emptied
     * first because a frame is the whole window, not the difference. */
    if (st->dl != NULL) {
        tiku_dl_clear(st->dl);
    }
    st->surface->record = st->dl;
    tiku_rect_t page = { 0, 0, EDIT_W, EDIT_H - STRIP_H };
    tiku_rect_t strip = { 0, EDIT_H - STRIP_H, EDIT_W, STRIP_H };
    char where[128];
    int i;

    tiku_fill(st->surface, page, TIKU_C_DOC);
    for (i = 0; i < rows &&
             tiku_textview_top(&st->tv) + i < tiku_textview_lines(&st->tv);
         i++) {
        int y = MARGIN + i * step;

        tiku_text(st->surface, f, MARGIN, y + f->ascent,
                       tiku_textview_line(&st->tv,
                                          tiku_textview_top(&st->tv) + i),
                       TIKU_C_TEXT);
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
    int rows = rows_visible();
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
