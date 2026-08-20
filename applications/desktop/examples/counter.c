/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * counter.c - buttons, keys and menus reaching one piece of state.
 *
 * Three routes to the same value: a click inside a drawn button, a key,
 * and a published menu row.  Shows hit-testing against content
 * coordinates, which is what a window's events carry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_desk_app.h"
#include "tiku_desk_client.h"
#include "tiku_desk_font.h"
#include "tiku_desk_gfx.h"

#define COUNTER_W 280
#define COUNTER_H 150

#define CMD_QUIT  1
#define CMD_UP    2
#define CMD_DOWN  3
#define CMD_RESET 4

typedef struct {
    const tiku_desk_app_services_t *services;
    tiku_desk_surface_t            *surface;
    uint32_t                        id;
    int                             value;
    int                             held;   /* button drawn pressed */
} counter_state_t;

/* Where the two buttons are, in the window's own coordinates. */
static const tiku_desk_rect_t MINUS = { 30, 86, 46, 30 };
static const tiku_desk_rect_t PLUS = { 204, 86, 46, 30 };

static int
inside(tiku_desk_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void
button(tiku_desk_surface_t *s, tiku_desk_rect_t r, const char *label,
       int pressed)
{
    tiku_desk_rgb_t light = tiku_desk_tint(TIKU_DESK_C_PANEL,
                                           TIKU_DESK_LIGHTEN_MAX);
    tiku_desk_rgb_t dark = tiku_desk_tint(TIKU_DESK_C_PANEL,
                                          TIKU_DESK_DARKEN_2);

    tiku_desk_fill(s, r, TIKU_DESK_C_PANEL);
    tiku_desk_frame(s, r, dark);
    /* Pressed is the same bevel with the light and dark swapped: the
     * control appears to go in, rather than being redrawn as a second
     * picture that has to be kept in step with the first. */
    tiku_desk_bevel(s, tiku_desk_inset(r, 1), pressed ? dark : light,
                    pressed ? light : dark);
    (void)tiku_desk_text_centered(s, tiku_desk_font_bold(), r, label,
                                  TIKU_DESK_C_TEXT);
}

static void
paint(counter_state_t *st)
{
    const tiku_desk_font_t *big = tiku_desk_font_at(30);
    tiku_desk_rect_t all = { 0, 0, COUNTER_W, COUNTER_H };
    char shown[32];

    snprintf(shown, sizeof shown, "%d", st->value);
    tiku_desk_fill(st->surface, all, TIKU_DESK_C_PANEL);
    tiku_desk_fill(st->surface, (tiku_desk_rect_t){ 30, 22, 220, 48 },
                   TIKU_DESK_C_DOC);
    tiku_desk_frame(st->surface, (tiku_desk_rect_t){ 30, 22, 220, 48 },
                    tiku_desk_tint(TIKU_DESK_C_PANEL,
                                   TIKU_DESK_DARKEN_2));
    (void)tiku_desk_text_centered(st->surface, big,
                                  (tiku_desk_rect_t){ 30, 30, 220,
                                                      big->height },
                                  shown, TIKU_DESK_C_TEXT);
    button(st->surface, MINUS, "-", st->held == CMD_DOWN);
    button(st->surface, PLUS, "+", st->held == CMD_UP);
    (void)tiku_desk_text_centered(st->surface,
                                  tiku_desk_font_plain(),
                                  (tiku_desk_rect_t){ 76, 92, 128, 18 },
                                  "up / down keys", TIKU_DESK_C_TEXT);
    (void)st->services->frame(st->services->ctx, st->id, st->surface->px,
                              COUNTER_W, COUNTER_H);
}

static void
publish(counter_state_t *st)
{
    tiku_desk_menuset_t menus;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Count");
    menus.menu[0].nitem = 4;
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "Increase");
    menus.menu[0].item[0].command = CMD_UP;
    menus.menu[0].item[0].enabled = 1;
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Decrease");
    menus.menu[0].item[1].command = CMD_DOWN;
    /* A row that would do nothing is offered disabled rather than
     * hidden, so the menu keeps its shape as the value moves. */
    menus.menu[0].item[1].enabled = (unsigned char)(st->value > 0);
    snprintf(menus.menu[0].item[2].label,
             sizeof menus.menu[0].item[2].label, "Reset");
    menus.menu[0].item[2].command = CMD_RESET;
    menus.menu[0].item[2].enabled = (unsigned char)(st->value != 0);
    snprintf(menus.menu[0].item[3].label,
             sizeof menus.menu[0].item[3].label, "Quit");
    menus.menu[0].item[3].command = CMD_QUIT;
    menus.menu[0].item[3].enabled = 1;
    (void)st->services->menus(st->services->ctx, st->id, &menus);
}

static void
change(counter_state_t *st, int by)
{
    int next = st->value + by;

    if (next < 0) {
        return;                 /* the floor is not an error, just a stop */
    }
    st->value = next;
    publish(st);                /* the enabled rows follow the value */
    paint(st);
}

static int
counter_start(void **state, const tiku_desk_app_services_t *services)
{
    counter_state_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->surface = tiku_desk_surface_new(COUNTER_W, COUNTER_H,
                                        TIKU_DESK_C_PANEL);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    st->id = services->open(services->ctx, "Counter", COUNTER_W,
                            COUNTER_H);
    paint(st);
    publish(st);
    *state = st;
    return 0;
}

static void
counter_stop(void *state)
{
    counter_state_t *st = state;

    if (st != NULL) {
        tiku_desk_surface_free(st->surface);
        free(st);
    }
}

static int
counter_event(void *state, const tiku_desk_event_t *event)
{
    counter_state_t *st = state;

    switch (event->type) {
    case TIKU_DESK_EVENT_POINTER_DOWN:
        /* Coordinates arrive relative to the window's CONTENT, so the
         * same hit test works wherever the window sits. */
        if (inside(PLUS, event->x, event->y)) {
            st->held = CMD_UP;
            paint(st);
        } else if (inside(MINUS, event->x, event->y)) {
            st->held = CMD_DOWN;
            paint(st);
        }
        break;
    case TIKU_DESK_EVENT_POINTER_UP:
        /* The press commits only if the release lands on the same
         * control: a drag away from a button is a cancelled press. */
        if (st->held == CMD_UP && inside(PLUS, event->x, event->y)) {
            st->held = 0;
            change(st, 1);
        } else if (st->held == CMD_DOWN &&
                   inside(MINUS, event->x, event->y)) {
            st->held = 0;
            change(st, -1);
        } else if (st->held != 0) {
            st->held = 0;
            paint(st);
        }
        break;
    case TIKU_DESK_EVENT_KEY_DOWN:
        if (event->key == TIKU_DESK_KEY_ESCAPE) {
            return 1;
        }
        if (event->key == TIKU_DESK_KEY_UP) {
            change(st, 1);
        }
        if (event->key == TIKU_DESK_KEY_DOWN) {
            change(st, -1);
        }
        break;
    default:
        break;
    }
    return 0;
}

static int
counter_pick(void *state, uint32_t window, int command)
{
    counter_state_t *st = state;

    (void)window;
    switch (command) {
    case CMD_QUIT:
        return 1;
    case CMD_UP:
        change(st, 1);
        break;
    case CMD_DOWN:
        change(st, -1);
        break;
    case CMD_RESET:
        change(st, -st->value);
        break;
    default:
        break;
    }
    return 0;
}

const tiku_desk_app_descriptor_t tiku_example_counter = {
    .id = "org.tikuos.example.counter",
    .name = "Counter",
    .start = counter_start,
    .stop = counter_stop,
    .event = counter_event,
    .pick = counter_pick
};

#ifndef TIKU_EXAMPLE_EMBED
int
main(void)
{
    return tiku_desk_client_run(&tiku_example_counter);
}
#endif
