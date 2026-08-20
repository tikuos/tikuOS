/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_remote.c - the window session over a local stream.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <signal.h>

#include "tiku_desk_remote.h"

/** @brief One frame's ceiling, from the dimension cap. */
#define REMOTE_MAX_PAYLOAD \
    (12u + 4u * TIKU_DESK_REMOTE_MAX_DIM * TIKU_DESK_REMOTE_MAX_DIM)

int
tiku_desk_remote_path(char *out, size_t max)
{
    const char *home = getenv("HOME");

    if (home == NULL || home[0] == '\0') {
        return -1;
    }
    if ((size_t)snprintf(out, max, "%s/.config/tracker/desk.sock",
                         home) >= max) {
        return -1;
    }
    return 0;
}

static int
set_nonblock(int fd)
{
    /* The line may die under us; a write then must report, not kill. */
    (void)signal(SIGPIPE, SIG_IGN);

    int flags = fcntl(fd, F_GETFL, 0);

    return (flags >= 0) ? fcntl(fd, F_SETFL, flags | O_NONBLOCK) : -1;
}

/** @brief Send all of it; a stalled peer forfeits the message. */
static void
send_all(int fd, const void *data, size_t n)
{
    const unsigned char *p = data;

    while (n > 0u) {
        ssize_t wrote = write(fd, p, n);

        if (wrote <= 0) {
            if (wrote < 0 && (errno == EAGAIN || errno == EINTR)) {
                continue;       /* a frame is worth a brief stall */
            }
            return;
        }
        p += wrote;
        n -= (size_t)wrote;
    }
}

static void
send_msg(int fd, uint32_t type, const void *payload, uint32_t n)
{
    uint32_t head[2];

    head[0] = type;
    head[1] = n;
    send_all(fd, head, sizeof head);
    if (n > 0u) {
        send_all(fd, payload, n);
    }
}

/*---------------------------------------------------------------------------*/
/* Listener                                                                  */
/*---------------------------------------------------------------------------*/

static void
session_free(tiku_desk_remote_session_t *s)
{
    if (s->fd >= 0) {
        (void)close(s->fd);
    }
    free(s->frame);
    free(s->buf);
    memset(s, 0, sizeof *s);
    s->fd = -1;
}

int
tiku_desk_remote_listen(tiku_desk_remote_listener_t *listener)
{
    struct sockaddr_un addr;
    char dir[108];
    char *slash;
    int i;

    memset(listener, 0, sizeof *listener);
    listener->fd = -1;
    for (i = 0; i < TIKU_DESK_REMOTE_SESSIONS; i++) {
        listener->session[i].fd = -1;
    }
    if (tiku_desk_remote_path(listener->path,
                              sizeof listener->path) != 0) {
        return -1;
    }
    /* EVERY directory on the way, not just the last: a fresh home has no
     * .config either, and one mkdir of .config/tracker fails on the
     * missing parent -- leaving a desktop no application can reach. */
    snprintf(dir, sizeof dir, "%s", listener->path);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        char *step;

        *slash = '\0';
        for (step = strchr(dir + 1, '/'); step != NULL;
             step = strchr(step + 1, '/')) {
            *step = '\0';
            (void)mkdir(dir, 0700);
            *step = '/';
        }
        (void)mkdir(dir, 0700);
    }
    (void)unlink(listener->path);
    listener->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener->fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", listener->path);
    if (bind(listener->fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(listener->fd, 4) != 0) {
        (void)close(listener->fd);
        listener->fd = -1;
        return -1;
    }
    (void)set_nonblock(listener->fd);
    return 0;
}

void
tiku_desk_remote_shutdown(tiku_desk_remote_listener_t *listener)
{
    int i;

    for (i = 0; i < TIKU_DESK_REMOTE_SESSIONS; i++) {
        if (listener->session[i].used) {
            session_free(&listener->session[i]);
        }
    }
    if (listener->fd >= 0) {
        (void)close(listener->fd);
        (void)unlink(listener->path);
        listener->fd = -1;
    }
}

/** @brief Apply one complete message to its session. */
static int
session_message(tiku_desk_remote_session_t *s)
{
    const unsigned char *p = s->buf;
    int changed = 0;

    switch (s->cur_type) {
    case TIKU_DESK_RMSG_HELLO:
        if (s->want >= 4u + 32u) {
            uint32_t version;

            memcpy(&version, p, 4);
            if (version != TIKU_DESK_REMOTE_VERSION) {
                return -1;      /* wrong world: drop the session */
            }
            memcpy(s->name, p + 4, 32);
            s->name[31] = '\0';
        }
        break;
    case TIKU_DESK_RMSG_OPEN:
        if (s->want >= 12u + TIKU_DESK_REMOTE_TITLE) {
            int32_t w, h;

            memcpy(&s->win_id, p, 4);
            memcpy(&w, p + 4, 4);
            memcpy(&h, p + 8, 4);
            memcpy(s->title, p + 12, TIKU_DESK_REMOTE_TITLE);
            s->title[TIKU_DESK_REMOTE_TITLE - 1] = '\0';
            if (w < 1 || h < 1 || w > TIKU_DESK_REMOTE_MAX_DIM ||
                h > TIKU_DESK_REMOTE_MAX_DIM) {
                return -1;
            }
            s->open_w = w;
            s->open_h = h;
            s->opened = 1;
            changed = 1;
        }
        break;
    case TIKU_DESK_RMSG_FRAME:
        if (s->want >= 12u) {
            int32_t w, h;
            uint32_t id;

            memcpy(&id, p, 4);
            memcpy(&w, p + 4, 4);
            memcpy(&h, p + 8, 4);
            if (id == s->win_id && w >= 1 && h >= 1 &&
                w <= TIKU_DESK_REMOTE_MAX_DIM &&
                h <= TIKU_DESK_REMOTE_MAX_DIM &&
                s->want >= 12u + 4u * (size_t)w * (size_t)h) {
                uint32_t *px = realloc(s->frame,
                                       4u * (size_t)w * (size_t)h);

                if (px != NULL) {
                    memcpy(px, p + 12, 4u * (size_t)w * (size_t)h);
                    s->frame = px;
                    s->fw = w;
                    s->fh = h;
                    changed = 1;
                }
            }
        }
        break;
    case TIKU_DESK_RMSG_MENUS:
        if (s->want >= 4u + sizeof(tiku_desk_menuset_t)) {
            memcpy(&s->menus, p + 4, sizeof s->menus);
            s->has_menus = 1;
            s->menus_fresh = 1;
            changed = 1;
        }
        break;
    case TIKU_DESK_RMSG_CLOSE:
        return -1;              /* the client is done: drop the session */
    default:
        break;
    }
    return changed;
}

/** @brief Read whatever is ready.  @return change, or -1 when it died. */
static int
session_read(tiku_desk_remote_session_t *s)
{
    int changed = 0;

    for (;;) {
        if (s->want == 0u) {
            uint32_t head[2];

            while (s->hgot < sizeof s->hbuf) {
                ssize_t got = read(s->fd, s->hbuf + s->hgot,
                                   sizeof s->hbuf - s->hgot);

                if (got == 0) {
                    return -1;
                }
                if (got < 0) {
                    return (errno == EAGAIN || errno == EWOULDBLOCK ||
                            errno == EINTR) ? changed : -1;
                }
                s->hgot += (size_t)got;
            }
            memcpy(head, s->hbuf, sizeof head);
            s->hgot = 0u;
            if (head[1] > REMOTE_MAX_PAYLOAD) {
                return -1;      /* nonsense length: protect the shell */
            }
            s->cur_type = head[0];
            s->want = head[1];
            s->got = 0u;
            free(s->buf);
            s->buf = (s->want > 0u) ? malloc(s->want) : NULL;
            if (s->want > 0u && s->buf == NULL) {
                return -1;
            }
            if (s->want == 0u) {
                int r = session_message(s);

                if (r < 0) {
                    return -1;
                }
                changed |= r;
                continue;
            }
        }
        while (s->got < s->want) {
            ssize_t got = read(s->fd, s->buf + s->got, s->want - s->got);

            if (got == 0) {
                return -1;
            }
            if (got < 0) {
                return (errno == EAGAIN || errno == EWOULDBLOCK ||
                        errno == EINTR) ? changed : -1;
            }
            s->got += (size_t)got;
        }
        {
            int r = session_message(s);

            if (r < 0) {
                return -1;
            }
            changed |= r;
        }
        free(s->buf);
        s->buf = NULL;
        s->want = 0u;
        s->got = 0u;
    }
}

int
tiku_desk_remote_adopt(tiku_desk_remote_listener_t *listener, int fd)
{
    int i;

    for (i = 0; i < TIKU_DESK_REMOTE_SESSIONS; i++) {
        if (!listener->session[i].used) {
            memset(&listener->session[i], 0, sizeof listener->session[i]);
            listener->session[i].fd = fd;
            listener->session[i].used = 1;
            (void)set_nonblock(fd);
            return 0;
        }
    }
    return -1;
}

int
tiku_desk_remote_poll(tiku_desk_remote_listener_t *listener)
{
    int changed = 0;
    int i;

    while (listener->fd >= 0) {
        int fd = accept(listener->fd, NULL, NULL);

        if (fd < 0) {
            break;
        }
        for (i = 0; i < TIKU_DESK_REMOTE_SESSIONS; i++) {
            if (!listener->session[i].used) {
                memset(&listener->session[i], 0,
                       sizeof listener->session[i]);
                listener->session[i].fd = fd;
                listener->session[i].used = 1;
                (void)set_nonblock(fd);
                fd = -1;
                break;
            }
        }
        if (fd >= 0) {
            (void)close(fd);    /* full house */
        }
    }
    for (i = 0; i < TIKU_DESK_REMOTE_SESSIONS; i++) {
        tiku_desk_remote_session_t *s = &listener->session[i];
        int r;

        if (!s->used) {
            continue;
        }
        r = session_read(s);
        if (r < 0) {
            session_free(s);
            changed = 1;
        } else {
            /* A frame that arrived and never marked a repaint is a
             * window one event behind its own application. */
            changed |= r;
        }
    }
    for (i = 0; i < TIKU_DESK_REMOTE_SESSIONS; i++) {
        if (listener->session[i].used) {
            changed |= listener->session[i].opened &&
                       listener->session[i].window == NULL;
            changed |= listener->session[i].menus_fresh;
        }
    }
    return changed;
}

tiku_desk_remote_session_t *
tiku_desk_remote_owner(tiku_desk_remote_listener_t *listener,
                       struct tiku_desk_window *window)
{
    int i;

    if (window == NULL) {
        return NULL;
    }
    for (i = 0; i < TIKU_DESK_REMOTE_SESSIONS; i++) {
        if (listener->session[i].used &&
            listener->session[i].window == window) {
            return &listener->session[i];
        }
    }
    return NULL;
}

void
tiku_desk_remote_event(tiku_desk_remote_listener_t *listener,
                       struct tiku_desk_window *window,
                       const tiku_desk_event_t *event)
{
    tiku_desk_remote_session_t *s = tiku_desk_remote_owner(listener,
                                                           window);
    unsigned char payload[4 + sizeof *event];

    if (s == NULL) {
        return;
    }
    memcpy(payload, &s->win_id, 4);
    memcpy(payload + 4, event, sizeof *event);
    send_msg(s->fd, TIKU_DESK_RMSG_EVENT, payload, sizeof payload);
}

void
tiku_desk_remote_pick(tiku_desk_remote_listener_t *listener,
                      struct tiku_desk_window *window, int command)
{
    tiku_desk_remote_session_t *s = tiku_desk_remote_owner(listener,
                                                           window);
    unsigned char payload[8];
    int32_t c = command;

    if (s == NULL) {
        return;
    }
    memcpy(payload, &s->win_id, 4);
    memcpy(payload + 4, &c, 4);
    send_msg(s->fd, TIKU_DESK_RMSG_PICK, payload, sizeof payload);
}

void
tiku_desk_remote_window_closed(tiku_desk_remote_listener_t *listener,
                               struct tiku_desk_window *window)
{
    tiku_desk_remote_session_t *s = tiku_desk_remote_owner(listener,
                                                           window);

    if (s == NULL) {
        return;
    }
    send_msg(s->fd, TIKU_DESK_RMSG_CLOSED, &s->win_id, 4);
    s->window = NULL;
    s->opened = 0;
}

/*---------------------------------------------------------------------------*/
/* Client                                                                    */
/*---------------------------------------------------------------------------*/

int
tiku_desk_remote_connect(tiku_desk_remote_client_t *client,
                         const char *name, int wait_ms)
{
    struct sockaddr_un addr;
    char path[108];
    int waited = 0;

    memset(client, 0, sizeof *client);
    client->fd = -1;
    client->next_id = 1;
    if (tiku_desk_remote_path(path, sizeof path) != 0) {
        return -1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
    for (;;) {
        client->fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client->fd < 0) {
            return -1;
        }
        if (connect(client->fd, (struct sockaddr *)&addr,
                    sizeof addr) == 0) {
            break;
        }
        (void)close(client->fd);
        client->fd = -1;
        if (waited >= wait_ms) {
            return -1;
        }
        usleep(100000);
        waited += 100;
    }
    return tiku_desk_remote_connect_fd(client, name, client->fd);
}

int
tiku_desk_remote_connect_fd(tiku_desk_remote_client_t *client,
                            const char *name, int fd)
{
    unsigned char payload[4 + 32];
    uint32_t version = TIKU_DESK_REMOTE_VERSION;

    if (fd < 0) {
        return -1;
    }
    client->fd = fd;
    if (client->next_id == 0u) {
        client->next_id = 1;
    }
    memset(payload, 0, sizeof payload);
    memcpy(payload, &version, 4);
    snprintf((char *)payload + 4, 32, "%s", (name != NULL) ? name : "app");
    send_msg(client->fd, TIKU_DESK_RMSG_HELLO, payload, sizeof payload);
    (void)set_nonblock(client->fd);
    return 0;
}

void
tiku_desk_remote_disconnect(tiku_desk_remote_client_t *client)
{
    if (client->fd >= 0) {
        send_msg(client->fd, TIKU_DESK_RMSG_CLOSE, NULL, 0);
        (void)close(client->fd);
        client->fd = -1;
    }
}

uint32_t
tiku_desk_remote_open(tiku_desk_remote_client_t *client, const char *title,
                      int w, int h)
{
    unsigned char payload[12 + TIKU_DESK_REMOTE_TITLE];
    uint32_t id = client->next_id++;
    int32_t ww = w, hh = h;

    if (client->fd < 0) {
        return 0;
    }
    memset(payload, 0, sizeof payload);
    memcpy(payload, &id, 4);
    memcpy(payload + 4, &ww, 4);
    memcpy(payload + 8, &hh, 4);
    snprintf((char *)payload + 12, TIKU_DESK_REMOTE_TITLE, "%s",
             (title != NULL) ? title : "");
    send_msg(client->fd, TIKU_DESK_RMSG_OPEN, payload, sizeof payload);
    return id;
}

int
tiku_desk_remote_frame(tiku_desk_remote_client_t *client, uint32_t id,
                       const uint32_t *px, int w, int h)
{
    size_t n = 12u + 4u * (size_t)w * (size_t)h;
    unsigned char *payload;
    int32_t ww = w, hh = h;

    if (client->fd < 0 || px == NULL || w < 1 || h < 1) {
        return -1;
    }
    payload = malloc(n);
    if (payload == NULL) {
        return -1;
    }
    memcpy(payload, &id, 4);
    memcpy(payload + 4, &ww, 4);
    memcpy(payload + 8, &hh, 4);
    memcpy(payload + 12, px, 4u * (size_t)w * (size_t)h);
    send_msg(client->fd, TIKU_DESK_RMSG_FRAME, payload, (uint32_t)n);
    free(payload);
    return 0;
}

int
tiku_desk_remote_menus(tiku_desk_remote_client_t *client, uint32_t id,
                       const tiku_desk_menuset_t *menus)
{
    size_t n = 4u + sizeof *menus;
    unsigned char *payload;

    if (client->fd < 0 || menus == NULL) {
        return -1;
    }
    payload = malloc(n);
    if (payload == NULL) {
        return -1;
    }
    memcpy(payload, &id, 4);
    memcpy(payload + 4, menus, sizeof *menus);
    send_msg(client->fd, TIKU_DESK_RMSG_MENUS, payload, (uint32_t)n);
    free(payload);
    return 0;
}

int
tiku_desk_remote_read(tiku_desk_remote_client_t *client, uint32_t *id,
                      tiku_desk_event_t *event, int *command)
{
    uint32_t head[2];
    ssize_t got;

    if (client->fd < 0) {
        return -1;
    }
    /* The header accumulates across calls: a serial line has no peek. */
    while (client->hgot < sizeof client->hbuf) {
        got = read(client->fd, client->hbuf + client->hgot,
                   sizeof client->hbuf - client->hgot);
        if (got == 0) {
            return -1;
        }
        if (got < 0) {
            return (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR) ? 0 : -1;
        }
        client->hgot += (size_t)got;
    }
    memcpy(head, client->hbuf, sizeof head);
    {
        size_t n = head[1];
        unsigned char *buf;
        size_t at = 0;

        if (head[1] > 4096u) {
            return -1;
        }
        buf = malloc((n > 0u) ? n : 1u);
        if (buf == NULL) {
            return -1;
        }
        while (at < n) {
            got = read(client->fd, buf + at, n - at);
            if (got == 0) {
                free(buf);
                return -1;
            }
            if (got < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR) {
                    continue;   /* mid-message: finish it */
                }
                free(buf);
                return -1;
            }
            at += (size_t)got;
        }
        client->hgot = 0u;
        if (id != NULL && head[1] >= 4u) {
            memcpy(id, buf, 4);
        }
        if (head[0] == TIKU_DESK_RMSG_EVENT && event != NULL &&
            head[1] >= 4u + sizeof *event) {
            memcpy(event, buf + 4, sizeof *event);
        }
        if (head[0] == TIKU_DESK_RMSG_PICK && command != NULL &&
            head[1] >= 8u) {
            int32_t c;

            memcpy(&c, buf + 4, 4);
            *command = c;
        }
        free(buf);
    }
    return (int)head[0];
}
