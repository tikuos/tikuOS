/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_client.c - the out-of-process runner for one descriptor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>

#include "tiku_client.h"
#include "tiku_dl.h"
#include "tiku_remote.h"

typedef struct {
    tiku_remote_client_t remote;
} client_ctx_t;

static uint32_t
svc_open(void *ctx, const char *title, int w, int h)
{
    client_ctx_t *c = ctx;

    return tiku_remote_open(&c->remote, title, w, h);
}

static int
svc_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    client_ctx_t *c = ctx;

    return tiku_remote_frame(&c->remote, id, px, w, h);
}

/**
 * @brief Hand the window over the cheap way when the cheap way is whole.
 *
 * Three things have to be true to send commands: the far end has to be
 * able to play them, the list has to describe the WHOLE window -- an
 * icon in it and it does not -- and it has to actually be smaller, which
 * for a window of four fills it is not.  Any of them false and the
 * pixels go, and nothing above here notices either way.
 */
static int
svc_present(void *ctx, uint32_t id, const struct tiku_dl *dl,
            const uint32_t *px, int w, int h)
{
    client_ctx_t *c = ctx;
    size_t frame = 4u * (size_t)w * (size_t)h;

    /*
     * FOUR things, and the first is new: the far end has to have SAID it
     * can play a stream.  It used to be assumed, which was survivable
     * only while both ends were built together -- an end that cannot
     * play a command steps over it (tiku_dl.h) and the window arrives
     * missing whatever it drew, with the list still saying it is whole.
     *
     * Zero features means the desktop has not answered yet, or is too
     * old to answer at all.  Both get the pixels, which every version
     * has always understood.  And a list carrying ICONS needs the far
     * end to have claimed those separately: an end that plays streams
     * but not icon commands would step over them and show a window
     * with holes where its art was.
     */
    if (dl != NULL &&
        (tiku_remote_peer_features(&c->remote) &
             TIKU_FEAT_COMMAND_STREAM) != 0u &&
        (tiku_dl_icons(dl) == 0 ||
         (tiku_remote_peer_features(&c->remote) &
              TIKU_FEAT_ICON_STREAM) != 0u) &&
        tiku_dl_misses(dl) == 0 &&
        tiku_dl_flat_size(dl) * 2u < frame &&
        tiku_remote_draw(&c->remote, id, dl, w, h)) {
        return 1;
    }
    return tiku_remote_frame(&c->remote, id, px, w, h);
}

static int
svc_menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    client_ctx_t *c = ctx;

    return tiku_remote_menus(&c->remote, id, set);
}

static void
svc_close(void *ctx, uint32_t id)
{
    (void)ctx;
    (void)id;
    /* One window per session today: closing is leaving. */
}

static int64_t
now_us(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int
pump(const tiku_app_descriptor_t *app, client_ctx_t *ctx)
{
    tiku_app_services_t services;
    void *state = NULL;

    memset(&services, 0, sizeof services);
    services.ctx = ctx;
    services.open = svc_open;
    services.frame = svc_frame;
    services.present = svc_present;
    services.menus = svc_menus;
    services.close = svc_close;
    if (app->start != NULL && app->start(&state, &services) != 0) {
        tiku_remote_disconnect(&ctx->remote);
        return 1;
    }
    for (;;) {
        int done = 0;
        int dead = 0;

        /* DRAIN what is queued before sleeping: a drag is many messages,
         * and one-per-nap starves it. */
        for (;;) {
            tiku_event_t event;
            uint32_t id = 0;
            int command = 0;
            int type = tiku_remote_read(&ctx->remote, &id, &event,
                                             &command);

            if (type == 0) {
                break;
            }
            if (type < 0 || type == TIKU_RMSG_CLOSED) {
                dead = 1;
                break;
            }
            if (type == TIKU_RMSG_EVENT && app->event != NULL &&
                app->event(state, &event)) {
                done = 1;
            }
            if (type == TIKU_RMSG_PICK && app->pick != NULL &&
                app->pick(state, id, command)) {
                done = 1;
            }
        }
        if (app->tick != NULL) {
            app->tick(state, now_us());
        }
        if (done || dead) {
            break;
        }
        {
            /* Woken by the desktop rather than by a clock: a keystroke
             * reaches the application as it arrives instead of at the
             * end of whatever nap was already running.
             *
             * The timeout stays SHORT and unconditional because tick is
             * how an application drives everything the session does not
             * carry -- a terminal's pty, a clock's minute -- and this
             * runtime cannot know what else it is waiting on. */
            struct pollfd fds;

            fds.fd = ctx->remote.fd;
            fds.events = POLLIN;
            fds.revents = 0;
            (void)poll(&fds, 1, TIKU_CLIENT_TICK_MS);
        }
    }
    if (app->stop != NULL) {
        app->stop(state);
    }
    tiku_remote_disconnect(&ctx->remote);
    return 0;
}

int
tiku_client_run(const tiku_app_descriptor_t *app)
{
    client_ctx_t ctx;

    if (app == NULL) {
        return 1;
    }
    memset(&ctx, 0, sizeof ctx);
    if (tiku_remote_connect(&ctx.remote,
                                 (app->name != NULL) ? app->name : "app",
                                 8000) != 0) {
        return 1;
    }
    return pump(app, &ctx);
}

int
tiku_client_run_fd(const tiku_app_descriptor_t *app, int fd)
{
    client_ctx_t ctx;

    if (app == NULL) {
        return 1;
    }
    memset(&ctx, 0, sizeof ctx);
    if (tiku_remote_connect_fd(&ctx.remote,
            (app->name != NULL) ? app->name : "app", fd) != 0) {
        return 1;
    }
    return pump(app, &ctx);
}
