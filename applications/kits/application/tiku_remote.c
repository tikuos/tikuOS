/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_remote.c - the window session over a local stream.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <signal.h>

#include "tiku_remote.h"

/** @brief One frame's ceiling, from the dimension cap. */
#define REMOTE_MAX_PAYLOAD \
    (12u + 4u * TIKU_REMOTE_MAX_DIM * TIKU_REMOTE_MAX_DIM)

/**
 * @brief What the desktop may send DOWN in one message.
 *
 * Frames go the other way, so nothing down here is large: an event, a
 * pick, or a message a shell composed.  A quarter of a megabyte is room
 * for a very talkative one and still a number, which is the point -- the
 * client is reading it into a buffer before it has looked at it.
 */
#define REMOTE_MAX_DOWN (256u * 1024u)

int
tiku_remote_path(char *out, size_t max)
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

static void session_unmap(tiku_remote_session_t *s);

static void
session_free(tiku_remote_session_t *s)
{
    session_unmap(s);
    if (s->fd >= 0) {
        (void)close(s->fd);
    }
    free(s->frame);
    free(s->buf);
    while (s->said_count > 0) {
        tiku_msg_free(s->said[s->said_head]);
        s->said_head = (s->said_head + 1) % TIKU_REMOTE_SAID;
        s->said_count--;
    }
    memset(s, 0, sizeof *s);
    s->fd = -1;
}

int
tiku_remote_listen(tiku_remote_listener_t *listener)
{
    struct sockaddr_un addr;
    char dir[108];
    char *slash;
    int i;

    memset(listener, 0, sizeof *listener);
    listener->fd = -1;
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        listener->session[i].fd = -1;
    }
    if (tiku_remote_path(listener->path,
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
tiku_remote_shutdown(tiku_remote_listener_t *listener)
{
    int i;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
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

/** @brief Give back a shared surface, if this session had one. */
static void
session_unmap(tiku_remote_session_t *s)
{
    if (s->shared != NULL) {
        (void)munmap(s->shared, s->shared_bytes);
        s->shared = NULL;
        s->shared_bytes = 0u;
        if (s->owner != NULL) {
            s->owner->mapped--;
        }
    }
}

/** @brief Apply one complete message to its session. */
static int
session_message(tiku_remote_session_t *s)
{
    const unsigned char *p = s->buf;
    int changed = 0;

    switch (s->cur_type) {
    case TIKU_RMSG_HELLO:
        if (s->want >= 4u + 32u) {
            uint32_t version;

            memcpy(&version, p, 4);
            if (version != TIKU_REMOTE_VERSION) {
                return -1;      /* wrong world: drop the session */
            }
            memcpy(s->name, p + 4, 32);
            s->name[31] = '\0';
            /* Longer means the peer said what else it can do.  A version
             * 1 hello is 36 bytes and stays welcome. */
            if (s->want >= 4u + 32u + 4u) {
                memcpy(&s->features, p + 36, 4);
            }
            {
                /*
                 * And say what THIS end can do, which nothing used to.
                 * A client cannot choose what to send without it, and
                 * the choice is not optional once the vocabulary can
                 * grow: an op the far end has not got is stepped over,
                 * so guessing wrong loses whatever it drew rather than
                 * failing.
                 */
                unsigned char reply[8];
                uint32_t version = TIKU_REMOTE_VERSION;
                uint32_t mine = TIKU_FEAT_COMMAND_STREAM |
                                TIKU_FEAT_ICON_STREAM |
                                TIKU_FEAT_SHARED_SURFACE;

                memcpy(reply, &version, 4);
                memcpy(reply + 4, &mine, 4);
                send_msg(s->fd, TIKU_RMSG_WELCOME, reply, sizeof reply);
            }
        }
        break;
    case TIKU_RMSG_SURFACE:
        if (s->want >= 12u + TIKU_REMOTE_SHM_NAME) {
            char name[TIKU_REMOTE_SHM_NAME];
            int32_t w, h;
            uint32_t id;

            memcpy(&id, p, 4);
            memcpy(&w, p + 4, 4);
            memcpy(&h, p + 8, 4);
            memcpy(name, p + 12, sizeof name);
            name[sizeof name - 1] = '\0';
            if (w < 1 || h < 1 || w > TIKU_REMOTE_MAX_DIM ||
                h > TIKU_REMOTE_MAX_DIM) {
                return -1;
            }
            session_unmap(s);
            {
                size_t bytes = 4u * (size_t)w * (size_t)h *
                               TIKU_REMOTE_BUFFERS;
                int fd = shm_open(name, O_RDWR, 0600);

                if (fd >= 0) {
                    void *at = mmap(NULL, bytes, PROT_READ, MAP_SHARED,
                                    fd, 0);

                    (void)close(fd);
                    /* Unlinked once mapped: the mapping keeps it alive,
                     * and a name left behind outlives the window. */
                    (void)shm_unlink(name);
                    if (at != MAP_FAILED) {
                        s->shared = at;
                        s->shared_bytes = bytes;
                        if (s->owner != NULL) {
                            s->owner->mapped++;
                        }
                        s->fw = w;
                        s->fh = h;
                        s->shown = 0;
                    }
                }
            }
            changed = 1;
        }
        break;
    case TIKU_RMSG_READY:
        if (s->want >= 8u && s->shared != NULL) {
            uint32_t index;

            memcpy(&index, p + 4, 4);
            /* Which half of the shared surface to show.  Nothing is
             * copied: the window draws out of the application's own
             * pixels. */
            s->shown = (index < TIKU_REMOTE_BUFFERS) ? (int)index : 0;
            changed = 1;
        }
        break;
    case TIKU_RMSG_OPEN:
        if (s->want >= 12u + TIKU_REMOTE_TITLE) {
            int32_t w, h;

            memcpy(&s->win_id, p, 4);
            memcpy(&w, p + 4, 4);
            memcpy(&h, p + 8, 4);
            memcpy(s->title, p + 12, TIKU_REMOTE_TITLE);
            s->title[TIKU_REMOTE_TITLE - 1] = '\0';
            if (w < 1 || h < 1 || w > TIKU_REMOTE_MAX_DIM ||
                h > TIKU_REMOTE_MAX_DIM) {
                return -1;
            }
            s->open_w = w;
            s->open_h = h;
            s->opened = 1;
            changed = 1;
        }
        break;
    case TIKU_RMSG_FRAME:
        if (s->want >= 12u) {
            int32_t w, h;
            uint32_t id;

            memcpy(&id, p, 4);
            memcpy(&w, p + 4, 4);
            memcpy(&h, p + 8, 4);
            if (id == s->win_id && w >= 1 && h >= 1 &&
                w <= TIKU_REMOTE_MAX_DIM &&
                h <= TIKU_REMOTE_MAX_DIM &&
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
    case TIKU_RMSG_DRAW:
        if (s->want >= 12u) {
            int32_t w, h;
            uint32_t id;

            memcpy(&id, p, 4);
            memcpy(&w, p + 4, 4);
            memcpy(&h, p + 8, 4);
            if (id == s->win_id && w >= 1 && h >= 1 &&
                w <= TIKU_REMOTE_MAX_DIM && h <= TIKU_REMOTE_MAX_DIM) {
                tiku_dl_t *dl = tiku_dl_unflatten(p + 12, s->want - 12u,
                                                  NULL);

                if (dl != NULL) {
                    uint32_t *px = realloc(s->frame,
                                           4u * (size_t)w * (size_t)h);

                    if (px != NULL) {
                        /*
                         * Played into the SAME buffer a copied frame
                         * lands in, so nothing downstream of here can
                         * tell which way the window arrived -- which is
                         * the point of putting this beside FRAME rather
                         * than above it.
                         */
                        tiku_surface_t face;

                        memset(px, 0, 4u * (size_t)w * (size_t)h);
                        memset(&face, 0, sizeof face);
                        face.px = px;
                        face.w = w;
                        face.h = h;
                        face.scale = 1;
                        face.clip.w = w;
                        face.clip.h = h;
                        (void)tiku_dl_play(dl, &face);
                        s->frame = px;
                        s->fw = w;
                        s->fh = h;
                        changed = 1;
                    }
                    tiku_dl_free(dl);
                }
            }
        }
        break;
    case TIKU_RMSG_MENUS:
        if (s->want >= 4u + sizeof(tiku_menuset_t)) {
            memcpy(&s->menus, p + 4, sizeof s->menus);
            s->has_menus = 1;
            s->menus_fresh = 1;
            changed = 1;
        }
        break;
    case TIKU_RMSG_SAY: {
        tiku_msg_t *m = tiku_msg_unflatten(p, s->want, NULL);

        if (m == NULL) {
            break;              /* not a message: the rest of the link is */
        }                       /* still good, so keep it and drop this   */
        if (s->said_count >= TIKU_REMOTE_SAID) {
            tiku_msg_free(m);
            s->said_lost++;     /* said, and counted, and let go */
            break;
        }
        s->said[(s->said_head + s->said_count) % TIKU_REMOTE_SAID] = m;
        s->said_count++;
        changed = 1;
        break;
    }
    case TIKU_RMSG_CLOSE:
        return -1;              /* the client is done: drop the session */
    default:
        break;
    }
    return changed;
}

/** @brief Read whatever is ready.  @return change, or -1 when it died. */
static int
session_read(tiku_remote_session_t *s)
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
tiku_remote_adopt(tiku_remote_listener_t *listener, int fd)
{
    int i;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (!listener->session[i].used) {
            memset(&listener->session[i], 0, sizeof listener->session[i]);
            listener->session[i].fd = fd;
            listener->session[i].used = 1;
            listener->session[i].owner = listener;
            (void)set_nonblock(fd);
            return 0;
        }
    }
    return -1;
}

int
tiku_remote_poll(tiku_remote_listener_t *listener)
{
    int changed = 0;
    int i;

    while (listener->fd >= 0) {
        int fd = accept(listener->fd, NULL, NULL);

        if (fd < 0) {
            break;
        }
        for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
            if (!listener->session[i].used) {
                memset(&listener->session[i], 0,
                       sizeof listener->session[i]);
                listener->session[i].fd = fd;
                listener->session[i].used = 1;
                listener->session[i].owner = listener;
                (void)set_nonblock(fd);
                fd = -1;
                break;
            }
        }
        if (fd >= 0) {
            (void)close(fd);    /* full house */
        }
    }
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        tiku_remote_session_t *s = &listener->session[i];
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
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (listener->session[i].used) {
            changed |= listener->session[i].opened &&
                       listener->session[i].window == NULL;
            changed |= listener->session[i].menus_fresh;
        }
    }
    return changed;
}

const uint32_t *
tiku_remote_pixels(const tiku_remote_session_t *session)
{
    if (session == NULL) {
        return NULL;
    }
    if (session->shared != NULL) {
        size_t one = (size_t)session->fw * (size_t)session->fh;

        return session->shared + (size_t)session->shown * one;
    }
    return session->frame;
}

int
tiku_remote_mapped(const tiku_remote_listener_t *listener)
{
    return (listener != NULL) ? listener->mapped : 0;
}

tiku_remote_session_t *
tiku_remote_owner(tiku_remote_listener_t *listener,
                       struct tiku_window *window)
{
    int i;

    if (window == NULL) {
        return NULL;
    }
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (listener->session[i].used &&
            listener->session[i].window == window) {
            return &listener->session[i];
        }
    }
    return NULL;
}

void
tiku_remote_event(tiku_remote_listener_t *listener,
                       struct tiku_window *window,
                       const tiku_event_t *event)
{
    tiku_remote_session_t *s = tiku_remote_owner(listener,
                                                           window);
    unsigned char payload[4 + sizeof *event];

    if (s == NULL) {
        return;
    }
    memcpy(payload, &s->win_id, 4);
    memcpy(payload + 4, event, sizeof *event);
    send_msg(s->fd, TIKU_RMSG_EVENT, payload, sizeof payload);
}

void
tiku_remote_pick(tiku_remote_listener_t *listener,
                      struct tiku_window *window, int command)
{
    tiku_remote_session_t *s = tiku_remote_owner(listener,
                                                           window);
    unsigned char payload[8];
    int32_t c = command;

    if (s == NULL) {
        return;
    }
    memcpy(payload, &s->win_id, 4);
    memcpy(payload + 4, &c, 4);
    send_msg(s->fd, TIKU_RMSG_PICK, payload, sizeof payload);
}

void
tiku_remote_window_closed(tiku_remote_listener_t *listener,
                               struct tiku_window *window)
{
    tiku_remote_session_t *s = tiku_remote_owner(listener,
                                                           window);

    if (s == NULL) {
        return;
    }
    send_msg(s->fd, TIKU_RMSG_CLOSED, &s->win_id, 4);
    s->window = NULL;
    s->opened = 0;
}

/*---------------------------------------------------------------------------*/
/* Client                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief The session at @p index, when there is a live one. */
static tiku_remote_session_t *
session_at(tiku_remote_listener_t *listener, int index)
{
    if (listener == NULL || index < 0 || index >= TIKU_REMOTE_SESSIONS) {
        return NULL;
    }
    return listener->session[index].used ? &listener->session[index] : NULL;
}

tiku_msg_t *
tiku_remote_said(tiku_remote_listener_t *listener, int index)
{
    tiku_remote_session_t *s = session_at(listener, index);
    tiku_msg_t *m;

    if (s == NULL || s->said_count == 0) {
        return NULL;
    }
    m = s->said[s->said_head];
    s->said_head = (s->said_head + 1) % TIKU_REMOTE_SAID;
    s->said_count--;
    return m;
}

int
tiku_remote_lost(const tiku_remote_listener_t *listener, int index)
{
    if (listener == NULL || index < 0 ||
        index >= TIKU_REMOTE_SESSIONS || !listener->session[index].used) {
        return 0;
    }
    return listener->session[index].said_lost;
}

/**
 * @brief Flatten @p m and send it as @p type down @p fd.
 *
 * The size is asked for first and the buffer taken to fit, so a message
 * is never half-written onto a wire the other end is reading in frames.
 */
static int
send_flat(int fd, uint32_t type, const tiku_msg_t *m)
{
    size_t n = tiku_msg_flat_size(m);
    unsigned char *flat;

    if (fd < 0 || n == 0u || n > REMOTE_MAX_PAYLOAD) {
        return 0;
    }
    flat = malloc(n);
    if (flat == NULL) {
        return 0;
    }
    if (!tiku_msg_flatten(m, flat, n, NULL)) {
        free(flat);
        return 0;
    }
    send_msg(fd, type, flat, (uint32_t)n);
    free(flat);
    return 1;
}

int
tiku_remote_tell(tiku_remote_listener_t *listener, int index,
                      const tiku_msg_t *m)
{
    tiku_remote_session_t *s = session_at(listener, index);

    return (s != NULL) ? send_flat(s->fd, TIKU_RMSG_TELL, m) : 0;
}

int
tiku_remote_connect(tiku_remote_client_t *client,
                         const char *name, int wait_ms)
{
    struct sockaddr_un addr;
    char path[108];
    int waited = 0;

    memset(client, 0, sizeof *client);
    client->fd = -1;
    client->next_id = 1;
    if (tiku_remote_path(path, sizeof path) != 0) {
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
    return tiku_remote_connect_fd(client, name, client->fd);
}

int
tiku_remote_connect_fd(tiku_remote_client_t *client,
                            const char *name, int fd)
{
    unsigned char payload[4 + 32 + 4];
    uint32_t version = TIKU_REMOTE_VERSION;
    uint32_t features = 0u;
    int type = 0;
    socklen_t len = sizeof type;

    if (fd < 0) {
        return -1;
    }
    /*
     * Start from nothing.  It mattered less when every field was a number
     * a caller could be trusted to have zeroed; it matters now that the
     * client HOLDS something -- the message it last heard -- because a
     * pointer nobody set is one disconnect away from being freed.
     */
    memset(client, 0, sizeof *client);
    client->fd = fd;
    client->next_id = 1;
    /* A shared surface means one machine.  A socket says so; a serial
     * line says the opposite, and getsockopt is how the difference is
     * asked rather than assumed. */
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0) {
        features |= TIKU_FEAT_SHARED_SURFACE;
    }
    client->features = features;
    memset(payload, 0, sizeof payload);
    memcpy(payload, &version, 4);
    snprintf((char *)payload + 4, 32, "%s", (name != NULL) ? name : "app");
    memcpy(payload + 36, &features, 4);
    send_msg(client->fd, TIKU_RMSG_HELLO, payload, sizeof payload);
    (void)set_nonblock(client->fd);
    return 0;
}

unsigned
tiku_remote_peer_features(const tiku_remote_client_t *client)
{
    return (client != NULL) ? client->peer_features : 0u;
}

int
tiku_remote_draw(tiku_remote_client_t *client, uint32_t id,
                      const tiku_dl_t *dl, int w, int h)
{
    size_t n;
    unsigned char *payload;
    int ok;

    if (client == NULL || client->fd < 0 || dl == NULL ||
        w < 1 || h < 1 || w > TIKU_REMOTE_MAX_DIM ||
        h > TIKU_REMOTE_MAX_DIM) {
        return 0;
    }
    n = tiku_dl_flat_size(dl);
    if (12u + n > REMOTE_MAX_PAYLOAD) {
        return 0;
    }
    payload = malloc(12u + n);
    if (payload == NULL) {
        return 0;
    }
    memcpy(payload, &id, 4);
    memcpy(payload + 4, &w, 4);
    memcpy(payload + 8, &h, 4);
    ok = (n == 0u) || tiku_dl_flatten(dl, payload + 12, n, NULL);
    if (ok) {
        send_msg(client->fd, TIKU_RMSG_DRAW, payload,
                 (uint32_t)(12u + n));
    }
    free(payload);
    return ok;
}

int
tiku_remote_say(tiku_remote_client_t *client,
                     const tiku_msg_t *m)
{
    return (client != NULL) ? send_flat(client->fd, TIKU_RMSG_SAY, m) : 0;
}

tiku_msg_t *
tiku_remote_heard(tiku_remote_client_t *client)
{
    tiku_msg_t *m;

    if (client == NULL) {
        return NULL;
    }
    m = client->heard;
    client->heard = NULL;
    return m;
}

void
tiku_remote_disconnect(tiku_remote_client_t *client)
{
    tiku_msg_free(client->heard);
    client->heard = NULL;
    if (client->shared != NULL) {
        (void)munmap(client->shared, client->shared_bytes);
        client->shared = NULL;
        client->shared_bytes = 0u;
    }
    if (client->fd >= 0) {
        send_msg(client->fd, TIKU_RMSG_CLOSE, NULL, 0);
        (void)close(client->fd);
        client->fd = -1;
    }
}

uint32_t
tiku_remote_open(tiku_remote_client_t *client, const char *title,
                      int w, int h)
{
    unsigned char payload[12 + TIKU_REMOTE_TITLE];
    uint32_t id = client->next_id++;
    int32_t ww = w, hh = h;

    if (client->fd < 0) {
        return 0;
    }
    memset(payload, 0, sizeof payload);
    memcpy(payload, &id, 4);
    memcpy(payload + 4, &ww, 4);
    memcpy(payload + 8, &hh, 4);
    snprintf((char *)payload + 12, TIKU_REMOTE_TITLE, "%s",
             (title != NULL) ? title : "");
    send_msg(client->fd, TIKU_RMSG_OPEN, payload, sizeof payload);
    return id;
}

/**
 * @brief Make a surface both processes can see, and tell the desktop.
 *
 * @return nonzero when the surface is there to paint into.
 */
static int
client_share(tiku_remote_client_t *client, uint32_t id, int w, int h)
{
    unsigned char payload[12 + TIKU_REMOTE_SHM_NAME];
    size_t bytes = 4u * (size_t)w * (size_t)h * TIKU_REMOTE_BUFFERS;
    int32_t ww = w, hh = h;
    void *at;
    int fd;

    if (client->shared != NULL && client->shared_w == w &&
        client->shared_h == h) {
        return 1;
    }
    if (client->shared != NULL) {
        (void)munmap(client->shared, client->shared_bytes);
        client->shared = NULL;
    }
    snprintf(client->shm_name, sizeof client->shm_name,
             "/tiku-desk-%ld-%u", (long)getpid(), (unsigned)id);
    (void)shm_unlink(client->shm_name);
    fd = shm_open(client->shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return 0;
    }
    if (ftruncate(fd, (off_t)bytes) != 0) {
        (void)close(fd);
        (void)shm_unlink(client->shm_name);
        return 0;
    }
    at = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    (void)close(fd);
    if (at == MAP_FAILED) {
        (void)shm_unlink(client->shm_name);
        return 0;
    }
    client->shared = at;
    client->shared_bytes = bytes;
    client->shared_w = w;
    client->shared_h = h;
    client->next_buffer = 0;
    memset(payload, 0, sizeof payload);
    memcpy(payload, &id, 4);
    memcpy(payload + 4, &ww, 4);
    memcpy(payload + 8, &hh, 4);
    snprintf((char *)payload + 12, TIKU_REMOTE_SHM_NAME, "%s",
             client->shm_name);
    send_msg(client->fd, TIKU_RMSG_SURFACE, payload, sizeof payload);
    /* The desktop unlinks the name once it has mapped it; this end keeps
     * only the mapping. */
    return 1;
}

int
tiku_remote_frame(tiku_remote_client_t *client, uint32_t id,
                       const uint32_t *px, int w, int h)
{
    size_t n = 12u + 4u * (size_t)w * (size_t)h;
    unsigned char *payload;
    int32_t ww = w, hh = h;

    if (client->fd < 0 || px == NULL || w < 1 || h < 1) {
        return -1;
    }
    if ((client->features & TIKU_FEAT_SHARED_SURFACE) != 0u &&
        client_share(client, id, w, h)) {
        size_t one = 4u * (size_t)w * (size_t)h;
        unsigned char ready[8];
        uint32_t index = (uint32_t)client->next_buffer;

        /* Painted into the half the desktop is NOT showing, so a frame
         * is never half-old and half-new on screen. */
        memcpy((unsigned char *)client->shared + index * one, px, one);
        memcpy(ready, &id, 4);
        memcpy(ready + 4, &index, 4);
        send_msg(client->fd, TIKU_RMSG_READY, ready, sizeof ready);
        client->next_buffer =
            (client->next_buffer + 1) % TIKU_REMOTE_BUFFERS;
        return 0;
    }
    payload = malloc(n);
    if (payload == NULL) {
        return -1;
    }
    memcpy(payload, &id, 4);
    memcpy(payload + 4, &ww, 4);
    memcpy(payload + 8, &hh, 4);
    memcpy(payload + 12, px, 4u * (size_t)w * (size_t)h);
    send_msg(client->fd, TIKU_RMSG_FRAME, payload, (uint32_t)n);
    free(payload);
    return 0;
}

int
tiku_remote_menus(tiku_remote_client_t *client, uint32_t id,
                       const tiku_menuset_t *menus)
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
    send_msg(client->fd, TIKU_RMSG_MENUS, payload, (uint32_t)n);
    free(payload);
    return 0;
}

int
tiku_remote_read(tiku_remote_client_t *client, uint32_t *id,
                      tiku_event_t *event, int *command)
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

        if (head[1] > REMOTE_MAX_DOWN) {
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
        if (head[0] == TIKU_RMSG_EVENT && event != NULL &&
            head[1] >= 4u + sizeof *event) {
            memcpy(event, buf + 4, sizeof *event);
        }
        if (head[0] == TIKU_RMSG_PICK && command != NULL &&
            head[1] >= 8u) {
            int32_t c;

            memcpy(&c, buf + 4, 4);
            *command = c;
        }
        if (head[0] == TIKU_RMSG_WELCOME && head[1] >= 8u) {
            uint32_t v, f;

            memcpy(&v, buf, 4);
            memcpy(&f, buf + 4, 4);
            client->peer_version = (int)v;
            client->peer_features = f;
        }
        if (head[0] == TIKU_RMSG_TELL) {
            tiku_msg_free(client->heard);   /* one at a time: a read */
            client->heard =                      /* nobody took is a read */
                tiku_msg_unflatten(buf, n, NULL);  /* nobody wanted  */
        }
        free(buf);
    }
    return (int)head[0];
}
