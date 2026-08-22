/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * clock.c - an application that redraws on its own, without input.
 *
 * Shows the tick entry point and the reason a frame is sent only when
 * something changed: the displayed second is compared, not the clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_gfx.h"

#define CLOCK_W 260
#define CLOCK_H 140

typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    uint32_t                        id;
    int                             shown_second;
    int                             seconds;    /* show them, or not */
} clock_state_t;

#define CMD_QUIT    1
#define CMD_SECONDS 2

static void
paint(clock_state_t *st)
{
    const tiku_font_t *big = tiku_font_at(34);
    const tiku_font_t *plain = tiku_font_plain();
    time_t now = time(NULL);
    struct tm tmv;
    char face[32];
    char date[64];
    tiku_rect_t all = { 0, 0, CLOCK_W, CLOCK_H };

    (void)localtime_r(&now, &tmv);
    if (strftime(face, sizeof face,
                 st->seconds ? "%H:%M:%S" : "%H:%M", &tmv) == 0) {
        snprintf(face, sizeof face, "--:--");
    }
    if (strftime(date, sizeof date, "%A %e %B", &tmv) == 0) {
        date[0] = '\0';
    }

    tiku_fill(st->surface, all, TIKU_C_PANEL);
    tiku_bevel(st->surface, all,
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_LIGHTEN_MAX),
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    (void)tiku_text_centered(st->surface, big,
                                  (tiku_rect_t){ 0, 24, CLOCK_W,
                                                      big->height },
                                  face, TIKU_C_TEXT);
    (void)tiku_text_centered(st->surface, plain,
                                  (tiku_rect_t){ 0, 84, CLOCK_W,
                                                      plain->height },
                                  date, TIKU_C_TEXT);
    (void)st->services->frame(st->services->ctx, st->id, st->surface->px,
                              CLOCK_W, CLOCK_H);
}

static void
publish(clock_state_t *st)
{
    tiku_menuset_t menus;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Clock");
    menus.menu[0].nitem = 2;
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "Show seconds");
    menus.menu[0].item[0].command = CMD_SECONDS;
    menus.menu[0].item[0].enabled = 1;
    /* The tick beside a menu row is state, so it is republished whenever
     * the state changes rather than set once at startup. */
    menus.menu[0].item[0].marked = (unsigned char)st->seconds;
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Quit");
    menus.menu[0].item[1].command = CMD_QUIT;
    menus.menu[0].item[1].enabled = 1;
    (void)st->services->menus(st->services->ctx, st->id, &menus);
}

static int
clock_start(void **state, const tiku_app_services_t *services)
{
    clock_state_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->shown_second = -1;
    st->surface = tiku_surface_new(CLOCK_W, CLOCK_H,
                                        TIKU_C_PANEL);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    st->id = services->open(services->ctx, "Clock", CLOCK_W, CLOCK_H);
    paint(st);
    publish(st);
    *state = st;
    return 0;
}

static void
clock_stop(void *state)
{
    clock_state_t *st = state;

    if (st != NULL) {
        tiku_surface_free(st->surface);
        free(st);
    }
}

/**
 * @brief Repaint when the DISPLAYED time changed, not when time passed.
 *
 * @note Called every turn of the host's loop, which is far more often
 *       than a clock face changes.
 */
static void
clock_tick(void *state, int64_t now_us)
{
    clock_state_t *st = state;
    time_t now = time(NULL);
    struct tm tmv;
    int mark;

    (void)now_us;
    (void)localtime_r(&now, &tmv);
    mark = st->seconds ? tmv.tm_sec : tmv.tm_min;
    if (mark != st->shown_second) {
        st->shown_second = mark;
        paint(st);
    }
}

static int
clock_event(void *state, const tiku_event_t *event)
{
    (void)state;
    return event->type == TIKU_EVENT_KEY_DOWN &&
           event->key == TIKU_KEY_ESCAPE;
}

static int
clock_pick(void *state, uint32_t window, int command)
{
    clock_state_t *st = state;

    (void)window;
    if (command == CMD_QUIT) {
        return 1;
    }
    if (command == CMD_SECONDS) {
        st->seconds = !st->seconds;
        st->shown_second = -1;
        publish(st);
        paint(st);
    }
    return 0;
}

const tiku_app_descriptor_t tiku_example_clock = {
    .id = "org.tikuos.example.clock",
    .name = "Clock",
    .start = clock_start,
    .stop = clock_stop,
    .event = clock_event,
    .tick = clock_tick,
    .pick = clock_pick
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_example_clock
};
#endif

#ifndef TIKU_EXAMPLE_EMBED
int
main(void)
{
    return tiku_client_run(&tiku_example_clock);
}
#endif
