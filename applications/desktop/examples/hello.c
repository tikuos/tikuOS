/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * hello.c - the smallest application the toolkit can run.
 *
 * A descriptor with one entry point: open a window, paint it once, done.
 * Building it with TIKU_EXAMPLE_EMBED omits main and leaves the
 * descriptor for a desktop to host in its own process.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include "tiku_desk_app.h"
#include "tiku_desk_client.h"
#include "tiku_desk_font.h"
#include "tiku_desk_gfx.h"

#define HELLO_W 300
#define HELLO_H 120

typedef struct {
    const tiku_desk_app_services_t *services;
    tiku_desk_surface_t            *surface;
    uint32_t                        id;
} hello_state_t;

static int
hello_start(void **state, const tiku_desk_app_services_t *services)
{
    const tiku_desk_font_t *font = tiku_desk_font_bold();
    hello_state_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    /* The application owns its pixels.  The services carry a finished
     * frame across; nothing draws into a window from outside. */
    st->surface = tiku_desk_surface_new(HELLO_W, HELLO_H,
                                        TIKU_DESK_C_PANEL);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    st->id = services->open(services->ctx, "Hello", HELLO_W, HELLO_H);

    tiku_desk_text(st->surface, font, 20, 40 + font->ascent,
                   "Hello from a descriptor.", TIKU_DESK_C_TEXT);
    tiku_desk_text(st->surface, tiku_desk_font_plain(), 20,
                   70 + font->ascent, "Escape closes this window.",
                   TIKU_DESK_C_TEXT);
    (void)services->frame(services->ctx, st->id, st->surface->px,
                          HELLO_W, HELLO_H);
    *state = st;
    return 0;
}

static void
hello_stop(void *state)
{
    hello_state_t *st = state;

    if (st != NULL) {
        tiku_desk_surface_free(st->surface);
        free(st);
    }
}

/** @brief @return nonzero when the application is finished. */
static int
hello_event(void *state, const tiku_desk_event_t *event)
{
    (void)state;
    return event->type == TIKU_DESK_EVENT_KEY_DOWN &&
           event->key == TIKU_DESK_KEY_ESCAPE;
}

const tiku_desk_app_descriptor_t tiku_example_hello = {
    .id = "org.tikuos.example.hello",
    .name = "Hello",
    .start = hello_start,
    .stop = hello_stop,
    .event = hello_event
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_desk_app.h. */
const tiku_desk_app_export_t tiku_desk_app_v1 = {
    TIKU_DESK_APP_ABI, (uint32_t)sizeof(tiku_desk_app_descriptor_t),
    &tiku_example_hello
};
#endif

#ifndef TIKU_EXAMPLE_EMBED
int
main(void)
{
    return tiku_desk_client_run(&tiku_example_hello);
}
#endif
