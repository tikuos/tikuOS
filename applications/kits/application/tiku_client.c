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

/*
 * The stamp that says "a TikuOS application built this".
 *
 * A listing cannot tell tiku-term from /bin/ls: both are executables, and
 * the type of a file here is decided by its NAME, which a program does
 * not have an extension for.  Reading the whole binary looking for a
 * descriptor id would cost megabytes per row.
 *
 * So the build says it instead, in a section of its own: a reader opens
 * the file, walks the section table -- a few hundred bytes near the head
 * -- and knows.  Anything that links this kit carries it, which is
 * exactly the set of things that ARE TikuOS applications.
 */
__attribute__((used, section(".tikuos")))
static const char tiku_app_stamp_client[] = "TikuOS-application-1";

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

/**
 * @brief Ask the shell to let the person pick a file.
 *
 * Over SAY, the road that already carries a self-describing message
 * upward: the answer comes back over TELL, and neither direction needs
 * an op of its own for one question.
 */
static int
svc_pick(void *ctx, uint32_t id, int mode, const char *start,
         const char *name)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_PICK);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_int32(m, "window", (int32_t)id);
    (void)tiku_msg_add_int32(m, "mode", (int32_t)mode);
    if (start != NULL) {
        (void)tiku_msg_add_string(m, "start", start);
    }
    if (name != NULL) {
        (void)tiku_msg_add_string(m, "name", name);
    }
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
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
    services.pick = svc_pick;
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
            if (type == TIKU_RMSG_TELL) {
                /* The shell said something.  One thing is worth hearing
                 * today: the path a pick() ended in. */
                tiku_msg_t *heard = tiku_remote_heard(&ctx->remote);

                if (heard != NULL) {
                    if (tiku_msg_what(heard) == TIKU_MSG_PICKED &&
                        app->picked != NULL) {
                        const char *path =
                            tiku_msg_find_string(heard, "path", 0);
                        int32_t win = 0;

                        (void)tiku_msg_find_int32(heard, "window", 0,
                                                  &win);
                        if (path != NULL) {
                            app->picked(state, (uint32_t)win, path);
                        }
                    }
                    tiku_msg_free(heard);
                }
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
