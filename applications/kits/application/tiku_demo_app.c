/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_demo_app.c - one descriptor, linked in or in its own process.
 *
 * The thesis in one artifact: this file draws through the services it is
 * handed and never asks where it is running.  Linked into the desktop it
 * is a window like any other; through tiku_client_run it is its own
 * process whose frames ride the session -- same code, same order, same
 * data.  It also paints a small trail under the pointer, because a drag
 * that crosses a process boundary should leave visible footprints.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "tiku_demo_app.h"
#include "tiku_gfx.h"
#include "tiku_logo.h"
#include "tiku_font.h"

#define DEMO_W 400
#define DEMO_H 210

#define CMD_QUIT    1
#define CMD_REFRESH 2

typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    uint32_t                        id;
    char                            kernel[96];
    long                            shown_minute;
} demo_state_t;

static long
read_uptime(void)
{
    FILE *f = fopen("/proc/uptime", "r");
    double up = 0.0;

    if (f != NULL) {
        (void)fscanf(f, "%lf", &up);
        (void)fclose(f);
    }
    return (long)up;
}

static void
paint(demo_state_t *st)
{
    tiku_surface_t *s = st->surface;
    const tiku_font_t *plain = tiku_font_plain();
    const tiku_font_t *big = tiku_font_at(28);
    const tiku_font_t *bold = tiku_font_bold();
    int facts_x = 150;
    int y = 20;
    char line[160];

    tiku_fill(s, (tiku_rect_t){ 0, 0, DEMO_W, DEMO_H },
                   TIKU_C_PANEL);
    {
        /* The same mark the Deskbar wears and the desktop's own About
         * puts up: this one runs in its own process and looks no
         * different, which is the claim it exists to make. */
        int mark = 72;
        int base;

        tiku_logo_paint(s, (tiku_rect_t){ 18, 16, mark, mark },
                        TIKU_LOGO_GROUND);
        base = 16 + mark + 12 + big->ascent;

        tiku_text(s, big, 18, base, "TikuOS", TIKU_C_TEXT);
        tiku_fill(s, (tiku_rect_t){ 18, base + 6, 96, 3 },
                       TIKU_C_TAB);
        tiku_text(s, bold, 18, base + 14 + plain->ascent,
                       "one descriptor", TIKU_C_TEXT);
    }
    tiku_text(s, bold, facts_x, y + plain->ascent,
                   "The same descriptor, either way", TIKU_C_TEXT);
    y += plain->height + 10;
    tiku_text(s, plain, facts_x, y + plain->ascent, st->kernel,
                   TIKU_C_TEXT);
    y += plain->height + 10;
    {
        long up = read_uptime();

        snprintf(line, sizeof line, "up %ld day%s, %ld:%02ld",
                 up / 86400, (up / 86400) == 1 ? "" : "s",
                 (up % 86400) / 3600, (up % 3600) / 60);
        tiku_text(s, plain, facts_x, y + plain->ascent, line,
                       TIKU_C_TEXT);
    }
    snprintf(line, sizeof line, "pid %ld: whose faults are whose",
             (long)getpid());
    tiku_text(s, plain, 18, DEMO_H - 14, line, TIKU_C_TEXT);
}

static void
show(demo_state_t *st)
{
    (void)st->services->frame(st->services->ctx, st->id,
                              st->surface->px, DEMO_W, DEMO_H);
}

static int
demo_start(void **state, const tiku_app_services_t *services)
{
    demo_state_t *st = calloc(1, sizeof *st);
    struct utsname un;

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->shown_minute = -1;
    if (uname(&un) == 0) {
        snprintf(st->kernel, sizeof st->kernel, "%s %s (%s)", un.sysname,
                 un.release, un.machine);
    } else {
        snprintf(st->kernel, sizeof st->kernel, "unknown kernel");
    }
    st->surface = tiku_surface_new(DEMO_W, DEMO_H,
                                        TIKU_C_PANEL);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    st->id = services->open(services->ctx, "Descriptor Demo", DEMO_W,
                            DEMO_H);
    paint(st);
    show(st);
    {
        tiku_menuset_t menus;

        memset(&menus, 0, sizeof menus);
        menus.nmenu = 1;
        snprintf(menus.menu[0].title, sizeof menus.menu[0].title,
                 "Remote");
        menus.menu[0].nitem = 2;
        snprintf(menus.menu[0].item[0].label,
                 sizeof menus.menu[0].item[0].label, "Refresh");
        menus.menu[0].item[0].command = CMD_REFRESH;
        menus.menu[0].item[0].enabled = 1;
        snprintf(menus.menu[0].item[1].label,
                 sizeof menus.menu[0].item[1].label, "Quit");
        menus.menu[0].item[1].command = CMD_QUIT;
        menus.menu[0].item[1].enabled = 1;
        (void)services->menus(services->ctx, st->id, &menus);
    }
    *state = st;
    return 0;
}

static void
demo_stop(void *state)
{
    demo_state_t *st = state;

    if (st != NULL) {
        tiku_surface_free(st->surface);
        free(st);
    }
}

static int
demo_event(void *state, const tiku_event_t *event)
{
    demo_state_t *st = state;

    if (event->type == TIKU_EVENT_KEY_DOWN &&
        event->key == TIKU_KEY_ESCAPE) {
        return 1;
    }
    if (event->type == TIKU_EVENT_POINTER_DOWN ||
        event->type == TIKU_EVENT_POINTER_MOVE ||
        event->type == TIKU_EVENT_POINTER_UP) {
        /* The footprint: a drag that crossed the boundary shows where it
         * went.  UP stamps in the text ink so the end is tellable. */
        tiku_fill(st->surface, (tiku_rect_t){ event->x - 2,
                       event->y - 2, 5, 5 },
                       event->type == TIKU_EVENT_POINTER_UP
                           ? TIKU_C_TEXT : TIKU_C_TAB);
        show(st);
    }
    return 0;
}

static void
demo_tick(void *state, int64_t now_us)
{
    demo_state_t *st = state;
    long minute = read_uptime() / 60;

    (void)now_us;
    if (minute != st->shown_minute) {
        /* Only the uptime LINE repaints: a tick that repainted the whole
         * panel would erase the pointer footprints, and liveness must
         * not destroy evidence. */
        const tiku_font_t *plain = tiku_font_plain();
        int y = 20 + 2 * (plain->height + 10);
        char line[64];
        long up = read_uptime();

        st->shown_minute = minute;
        tiku_fill(st->surface, (tiku_rect_t){ 150, y,
                       DEMO_W - 150, plain->height + 2 },
                       TIKU_C_PANEL);
        snprintf(line, sizeof line, "up %ld day%s, %ld:%02ld",
                 up / 86400, (up / 86400) == 1 ? "" : "s",
                 (up % 86400) / 3600, (up % 3600) / 60);
        tiku_text(st->surface, plain, 150, y + plain->ascent, line,
                       TIKU_C_TEXT);
        show(st);
    }
}

static int
demo_pick(void *state, uint32_t window, int command)
{
    demo_state_t *st = state;

    (void)window;
    if (command == CMD_QUIT) {
        return 1;
    }
    if (command == CMD_REFRESH) {
        paint(st);
        show(st);
    }
    return 0;
}

const tiku_app_descriptor_t tiku_demo_app = {
    .id = "org.tikuos.demo",
    .name = "Demo",
    .start = demo_start,
    .stop = demo_stop,
    .event = demo_event,
    .tick = demo_tick,
    .pick = demo_pick,
};
