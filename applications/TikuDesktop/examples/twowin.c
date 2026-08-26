/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * twowin.c - one application, two windows, kept apart.
 *
 * Every service the toolkit addresses BY WINDOW, exercised twice from
 * one application: two windows are opened, painted, given menus of
 * their own, typed into separately and closed one at a time.  What
 * carries the id and what quietly assumed there was only ever one is
 * the whole of what this shows.
 *
 * Each window writes what it has been typed into a file of its own,
 * because what a window RECEIVED is otherwise only a picture.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_gfx.h"

#define TWOWIN_W   240
#define TWOWIN_H   120
#define TWOWIN_DIR "/tmp/trk_twowin"

#define CMD_COUNT 1
#define CMD_CLOSE 2
#define CMD_OTHER 3

typedef struct {
    uint32_t             id;
    tiku_surface_t *surface;    /* NULL once the window has gone */
    int                  typed;
    const char          *name;       /* "alpha": the file it writes  */
    const char          *label;      /* "Alpha": what a person reads */
} pane_t;

typedef struct {
    const tiku_app_services_t *services;
    pane_t                          pane[2];
    int                             open_count;
} twowin_state_t;

/** @brief Put what this window has been typed where a test can read it. */
static void
record(const pane_t *p)
{
    char path[128];
    FILE *f;

    (void)mkdir(TWOWIN_DIR, 0777);
    snprintf(path, sizeof path, "%s/%s", TWOWIN_DIR, p->name);
    f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "%d\n", p->typed);
        (void)fclose(f);
    }
}

static void
paint(twowin_state_t *st, pane_t *p)
{
    tiku_rect_t all = { 0, 0, TWOWIN_W, TWOWIN_H };
    char shown[64];

    if (p->surface == NULL) {
        return;
    }
    snprintf(shown, sizeof shown, "%s typed %d", p->label, p->typed);
    tiku_fill(p->surface, all, TIKU_C_PANEL);
    (void)tiku_text_centered(p->surface, tiku_font_bold(), all,
                                  shown, TIKU_C_TEXT);
    (void)st->services->frame(st->services->ctx, p->id, p->surface->px,
                              TWOWIN_W, TWOWIN_H);
}

/**
 * @brief This window's own menu, named for it.
 *
 * Two windows publishing the same rows would show nothing: the strip
 * has to be seen carrying THIS window's rows while the other stands.
 */
static void
publish(twowin_state_t *st, pane_t *p)
{
    tiku_menuset_t menus;

    if (p->surface == NULL) {
        return;
    }
    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "%s",
             p->label);
    menus.menu[0].nitem = 3;
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "%s typed %d",
             p->label, p->typed);
    menus.menu[0].item[0].command = CMD_COUNT;
    menus.menu[0].item[0].enabled = 1;
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Close %s", p->label);
    menus.menu[0].item[1].command = CMD_CLOSE;
    menus.menu[0].item[1].enabled = 1;
    /* The row that reaches the application's OTHER window: what a
     * Window menu is, in the smallest form that can be proven. */
    snprintf(menus.menu[0].item[2].label,
             sizeof menus.menu[0].item[2].label, "Show the other one");
    menus.menu[0].item[2].command = CMD_OTHER;
    menus.menu[0].item[2].enabled = 1;
    (void)st->services->menus(st->services->ctx, p->id, &menus);
}

/** @brief The pane window @p id belongs to, or NULL once it has gone. */
static pane_t *
pane_of(twowin_state_t *st, uint32_t id)
{
    int i;

    for (i = 0; i < 2; i++) {
        if (st->pane[i].id == id && st->pane[i].surface != NULL) {
            return &st->pane[i];
        }
    }
    return NULL;
}

static void twowin_stop(void *state);

static int
twowin_start(void **state, const tiku_app_services_t *services)
{
    static const char *const NAME[2] = { "alpha", "beta" };
    static const char *const LABEL[2] = { "Alpha", "Beta" };
    twowin_state_t *st = calloc(1, sizeof *st);
    int i;

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    for (i = 0; i < 2; i++) {
        pane_t *p = &st->pane[i];
        char title[32];

        p->name = NAME[i];
        p->label = LABEL[i];
        p->surface = tiku_surface_new(TWOWIN_W, TWOWIN_H,
                                           TIKU_C_PANEL);
        if (p->surface == NULL) {
            twowin_stop(st);
            return -1;
        }
        /* One word, because a script addresses a window by a single
         * word of its title. */
        snprintf(title, sizeof title, "Two%s", p->label);
        p->id = services->open(services->ctx, title, TWOWIN_W,
                               TWOWIN_H);
        st->open_count++;
        record(p);
        paint(st, p);
        publish(st, p);
    }
    *state = st;
    return 0;
}

static void
twowin_stop(void *state)
{
    twowin_state_t *st = state;
    int i;

    if (st == NULL) {
        return;
    }
    for (i = 0; i < 2; i++) {
        tiku_surface_free(st->pane[i].surface);
    }
    free(st);
}

/** @brief Let go of one window, and say whether that was the last. */
static int
drop(twowin_state_t *st, pane_t *p)
{
    st->services->close(st->services->ctx, p->id);
    tiku_surface_free(p->surface);
    p->surface = NULL;
    st->open_count--;
    return st->open_count <= 0;
}

/**
 * @brief A key belongs to the window it was typed into.
 *
 * Without the id this counts for whichever window the application
 * guessed, and a guess is right half the time.
 */
static int
twowin_event_in(void *state, uint32_t window, const tiku_event_t *event)
{
    twowin_state_t *st = state;
    pane_t *p = pane_of(st, window);

    if (p == NULL || event->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    if (event->key < 32u || event->key >= 127u) {
        return 0;               /* the runtime's keys are not typing */
    }
    p->typed++;
    record(p);
    paint(st, p);
    publish(st, p);             /* the row carries the count */
    return 0;
}

static int
twowin_pick(void *state, uint32_t window, int command)
{
    twowin_state_t *st = state;
    pane_t *p = pane_of(st, window);

    if (p == NULL) {
        return 0;
    }
    if (command == CMD_CLOSE) {
        return drop(st, p);
    }
    if (command == CMD_OTHER) {
        pane_t *other = &st->pane[(p == &st->pane[0]) ? 1 : 0];

        if (other->surface != NULL &&
            st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             other->id);
        }
    }
    return 0;
}

/**
 * @brief The runtime took a window away.
 *
 * The application keeps the rest and ends when the last has gone,
 * which is what makes closing one window closing ONE window.
 */
static void
twowin_closed(void *state, uint32_t window)
{
    twowin_state_t *st = state;
    pane_t *p = pane_of(st, window);

    if (p != NULL) {
        tiku_surface_free(p->surface);
        p->surface = NULL;
        st->open_count--;
    }
}

const tiku_app_descriptor_t tiku_example_twowin = {
    .id = "org.tikuos.example.twowin",
    .name = "Two Windows",
    .start = twowin_start,
    .stop = twowin_stop,
    .pick = twowin_pick,
    .event_in = twowin_event_in,
    .closed = twowin_closed
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_example_twowin
};
#endif

#ifndef TIKU_EXAMPLE_EMBED
int
main(void)
{
    return tiku_client_run(&tiku_example_twowin);
}
#endif
