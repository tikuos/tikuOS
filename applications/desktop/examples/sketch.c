/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * sketch.c - a drag, followed from press to release.
 *
 * Ink is laid between the last point and this one, because a pointer
 * reports where it IS, not every pixel it crossed; a gesture drawn as
 * dots is a gesture drawn with holes in it.
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

#define SKETCH_W 320
#define SKETCH_H 220
#define STRIP_H  22

#define CMD_QUIT  1
#define CMD_CLEAR 2
#define CMD_INK   3

typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    uint32_t                        id;
    int                             drawing;
    int                             lx, ly;
    int                             dark_ink;
} sketch_state_t;

static tiku_rgb_t
ink_of(const sketch_state_t *st)
{
    return st->dark_ink ? TIKU_C_TEXT : TIKU_C_TAB;
}

static void
send(sketch_state_t *st)
{
    (void)st->services->frame(st->services->ctx, st->id, st->surface->px,
                              SKETCH_W, SKETCH_H);
}

static void
clear(sketch_state_t *st)
{
    tiku_rect_t paper = { 0, STRIP_H, SKETCH_W, SKETCH_H - STRIP_H };
    tiku_rect_t strip = { 0, 0, SKETCH_W, STRIP_H };

    tiku_fill(st->surface, strip, TIKU_C_PANEL);
    tiku_hline(st->surface, 0, STRIP_H - 1, SKETCH_W,
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_DARKEN_2));
    (void)tiku_text_centered(st->surface, tiku_font_plain(),
                                  strip, "drag to draw",
                                  TIKU_C_TEXT);
    tiku_fill(st->surface, paper, TIKU_C_DOC);
}

/** @brief Lay a round nib along the segment from (@p ax, @p ay). */
static void
stroke(sketch_state_t *st, int ax, int ay, int bx, int by)
{
    int dx = bx - ax;
    int dy = by - ay;
    int steps = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);
    int i;

    if (steps == 0) {
        steps = 1;
    }
    for (i = 0; i <= steps; i++) {
        int x = ax + dx * i / steps;
        int y = ay + dy * i / steps;

        if (y < STRIP_H + 2) {
            continue;           /* the strip is not paper */
        }
        tiku_fill(st->surface,
                       (tiku_rect_t){ x - 1, y - 1, 3, 3 },
                       ink_of(st));
    }
}

static void
publish(sketch_state_t *st)
{
    tiku_menuset_t menus;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Sketch");
    menus.menu[0].nitem = 3;
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "Clear");
    menus.menu[0].item[0].command = CMD_CLEAR;
    menus.menu[0].item[0].enabled = 1;
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Dark ink");
    menus.menu[0].item[1].command = CMD_INK;
    menus.menu[0].item[1].enabled = 1;
    menus.menu[0].item[1].marked = (unsigned char)st->dark_ink;
    snprintf(menus.menu[0].item[2].label,
             sizeof menus.menu[0].item[2].label, "Quit");
    menus.menu[0].item[2].command = CMD_QUIT;
    menus.menu[0].item[2].enabled = 1;
    (void)st->services->menus(st->services->ctx, st->id, &menus);
}

static int
sketch_start(void **state, const tiku_app_services_t *services)
{
    sketch_state_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->surface = tiku_surface_new(SKETCH_W, SKETCH_H,
                                        TIKU_C_DOC);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    st->id = services->open(services->ctx, "Sketch", SKETCH_W, SKETCH_H);
    clear(st);
    send(st);
    publish(st);
    *state = st;
    return 0;
}

static void
sketch_stop(void *state)
{
    sketch_state_t *st = state;

    if (st != NULL) {
        tiku_surface_free(st->surface);
        free(st);
    }
}

static int
sketch_event(void *state, const tiku_event_t *event)
{
    sketch_state_t *st = state;

    switch (event->type) {
    case TIKU_EVENT_POINTER_DOWN:
        st->drawing = 1;
        st->lx = event->x;
        st->ly = event->y;
        stroke(st, event->x, event->y, event->x, event->y);
        send(st);
        break;
    case TIKU_EVENT_POINTER_MOVE:
        /* Only while the button is down: a pointer crossing the window
         * on its way somewhere else is not a stroke. */
        if (st->drawing) {
            stroke(st, st->lx, st->ly, event->x, event->y);
            st->lx = event->x;
            st->ly = event->y;
            send(st);
        }
        break;
    case TIKU_EVENT_POINTER_UP:
        if (st->drawing) {
            stroke(st, st->lx, st->ly, event->x, event->y);
            st->drawing = 0;
            send(st);
        }
        break;
    case TIKU_EVENT_KEY_DOWN:
        if (event->key == TIKU_KEY_ESCAPE) {
            return 1;
        }
        break;
    default:
        break;
    }
    return 0;
}

static int
sketch_pick(void *state, uint32_t window, int command)
{
    sketch_state_t *st = state;

    (void)window;
    switch (command) {
    case CMD_QUIT:
        return 1;
    case CMD_CLEAR:
        clear(st);
        send(st);
        break;
    case CMD_INK:
        st->dark_ink = !st->dark_ink;
        publish(st);
        break;
    default:
        break;
    }
    return 0;
}

const tiku_app_descriptor_t tiku_example_sketch = {
    .id = "org.tikuos.example.sketch",
    .name = "Sketch",
    .start = sketch_start,
    .stop = sketch_stop,
    .event = sketch_event,
    .pick = sketch_pick
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_example_sketch
};
#endif

#ifndef TIKU_EXAMPLE_EMBED
int
main(void)
{
    return tiku_client_run(&tiku_example_sketch);
}
#endif
