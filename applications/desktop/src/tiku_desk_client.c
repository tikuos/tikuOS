/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_client.c - the out-of-process runner for one descriptor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>

#include "tiku_desk_client.h"
#include "tiku_desk_remote.h"

typedef struct {
    tiku_desk_remote_client_t remote;
} client_ctx_t;

static uint32_t
svc_open(void *ctx, const char *title, int w, int h)
{
    client_ctx_t *c = ctx;

    return tiku_desk_remote_open(&c->remote, title, w, h);
}

static int
svc_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    client_ctx_t *c = ctx;

    return tiku_desk_remote_frame(&c->remote, id, px, w, h);
}

static int
svc_menus(void *ctx, uint32_t id, const tiku_desk_menuset_t *set)
{
    client_ctx_t *c = ctx;

    return tiku_desk_remote_menus(&c->remote, id, set);
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
pump(const tiku_desk_app_descriptor_t *app, client_ctx_t *ctx)
{
    tiku_desk_app_services_t services;
    void *state = NULL;

    memset(&services, 0, sizeof services);
    services.ctx = ctx;
    services.open = svc_open;
    services.frame = svc_frame;
    services.menus = svc_menus;
    services.close = svc_close;
    if (app->start != NULL && app->start(&state, &services) != 0) {
        tiku_desk_remote_disconnect(&ctx->remote);
        return 1;
    }
    for (;;) {
        int done = 0;
        int dead = 0;

        /* DRAIN what is queued before sleeping: a drag is many messages,
         * and one-per-nap starves it. */
        for (;;) {
            tiku_desk_event_t event;
            uint32_t id = 0;
            int command = 0;
            int type = tiku_desk_remote_read(&ctx->remote, &id, &event,
                                             &command);

            if (type == 0) {
                break;
            }
            if (type < 0 || type == TIKU_DESK_RMSG_CLOSED) {
                dead = 1;
                break;
            }
            if (type == TIKU_DESK_RMSG_EVENT && app->event != NULL &&
                app->event(state, &event)) {
                done = 1;
            }
            if (type == TIKU_DESK_RMSG_PICK && app->pick != NULL &&
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
            (void)poll(&fds, 1, TIKU_DESK_CLIENT_TICK_MS);
        }
    }
    if (app->stop != NULL) {
        app->stop(state);
    }
    tiku_desk_remote_disconnect(&ctx->remote);
    return 0;
}

int
tiku_desk_client_run(const tiku_desk_app_descriptor_t *app)
{
    client_ctx_t ctx;

    if (app == NULL) {
        return 1;
    }
    memset(&ctx, 0, sizeof ctx);
    if (tiku_desk_remote_connect(&ctx.remote,
                                 (app->name != NULL) ? app->name : "app",
                                 8000) != 0) {
        return 1;
    }
    return pump(app, &ctx);
}

int
tiku_desk_client_run_fd(const tiku_desk_app_descriptor_t *app, int fd)
{
    client_ctx_t ctx;

    if (app == NULL) {
        return 1;
    }
    memset(&ctx, 0, sizeof ctx);
    if (tiku_desk_remote_connect_fd(&ctx.remote,
            (app->name != NULL) ? app->name : "app", fd) != 0) {
        return 1;
    }
    return pump(app, &ctx);
}
