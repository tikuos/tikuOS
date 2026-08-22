/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_conduct.c - the conductor channel (see the header).
 *
 * The framing is the window session's, deliberately: one wire format for
 * everything this desktop speaks, so the day these frames ride a serial
 * link there is one thing to carry and not two.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desk_conduct.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CONDUCT_MAX_PAYLOAD (64u * 1024u)

static int
set_nonblock(int fd)
{
    int flags;

    (void)signal(SIGPIPE, SIG_IGN);
    flags = fcntl(fd, F_GETFL, 0);
    return (flags >= 0) ? fcntl(fd, F_SETFL, flags | O_NONBLOCK) : -1;
}

static void
send_all(int fd, const void *data, size_t n)
{
    const unsigned char *p = data;

    while (n > 0u) {
        ssize_t wrote = write(fd, p, n);

        if (wrote <= 0) {
            if (wrote < 0 && (errno == EAGAIN || errno == EINTR)) {
                continue;
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
    if (n > 0u && payload != NULL) {
        send_all(fd, payload, n);
    }
}

/**
 * @brief Read one whole message into the buffer, without blocking.
 *
 * @return 1 when one is complete, 0 when not yet, -1 when the peer left.
 */
static int
stream_read(int fd, unsigned char *hbuf, size_t *hgot, unsigned char **buf,
            size_t *got, size_t *want, uint32_t *cur_type)
{
    if (*want == 0u) {
        while (*hgot < 8u) {
            ssize_t n = read(fd, hbuf + *hgot, 8u - *hgot);

            if (n > 0) {
                *hgot += (size_t)n;
                continue;
            }
            if (n == 0) {
                return -1;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        {
            uint32_t head[2];

            memcpy(head, hbuf, sizeof head);
            *hgot = 0u;
            if (head[1] > CONDUCT_MAX_PAYLOAD) {
                return -1;      /* nonsense on the wire: end it */
            }
            *cur_type = head[0];
            *want = head[1];
            *got = 0u;
            free(*buf);
            *buf = (*want > 0u) ? malloc(*want) : NULL;
            if (*want > 0u && *buf == NULL) {
                return -1;
            }
        }
    }
    while (*got < *want) {
        ssize_t n = read(fd, *buf + *got, *want - *got);

        if (n > 0) {
            *got += (size_t)n;
            continue;
        }
        if (n == 0) {
            return -1;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 1;
}

/*---------------------------------------------------------------------------*/
/* The shell's side                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_desk_conduct_listen(tiku_desk_conduct_t *c, const char *path,
                         void *ctx, tiku_desk_conduct_inject_fn inject,
                         tiku_desk_conduct_pixel_fn pixel,
                         tiku_desk_conduct_text_fn text)
{
    struct sockaddr_un addr;
    char dir[108];
    char *slash;

    if (c == NULL || path == NULL) {
        return -1;
    }
    memset(c, 0, sizeof *c);
    c->fd = -1;
    c->peer = -1;
    c->ctx = ctx;
    c->inject = inject;
    c->pixel = pixel;
    c->text = text;
    if ((size_t)snprintf(c->path, sizeof c->path, "%s", path) >=
        sizeof c->path) {
        return -1;
    }
    /* Every directory on the way; see tiku_desk_remote_listen. */
    snprintf(dir, sizeof dir, "%s", c->path);
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
    (void)unlink(c->path);
    c->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (c->fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", c->path);
    if (bind(c->fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(c->fd, 1) != 0) {
        (void)close(c->fd);
        c->fd = -1;
        return -1;
    }
    (void)set_nonblock(c->fd);
    return 0;
}

int
tiku_desk_conduct_adopt(tiku_desk_conduct_t *c, int fd, void *ctx,
                        tiku_desk_conduct_inject_fn inject,
                        tiku_desk_conduct_pixel_fn pixel,
                        tiku_desk_conduct_text_fn text)
{
    if (c == NULL || fd < 0) {
        return -1;
    }
    memset(c, 0, sizeof *c);
    c->fd = -1;                 /* nothing to listen on: the line is it */
    c->peer = fd;
    c->ctx = ctx;
    c->inject = inject;
    c->pixel = pixel;
    c->text = text;
    (void)set_nonblock(fd);
    return 0;
}

int
tiku_desk_conduct_open_tty(const char *path, int baud)
{
    struct termios tio;
    int fd;

    if (path == NULL) {
        return -1;
    }
    fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return -1;
    }
    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);
        /* cfmakeraw leaves VMIN 1, VTIME 0, and it must STAY that way.
         * With VMIN 0 a terminal read that has nothing to give returns
         * ZERO -- which on a socket means the peer hung up, and this code
         * reads both.  A line configured to poll would therefore look
         * like a cable being unplugged on every quiet moment.  Being
         * non-blocking is O_NONBLOCK's job; it reports EAGAIN, which is
         * a different thing from end-of-file and must remain so. */
        if (baud > 0) {
            speed_t sp = (baud == 9600)     ? B9600
                       : (baud == 19200)    ? B19200
                       : (baud == 38400)    ? B38400
                       : (baud == 57600)    ? B57600
                       : (baud == 230400)   ? B230400
                       : (baud == 460800)   ? B460800
                       : (baud == 921600)   ? B921600
                                            : B115200;

            (void)cfsetispeed(&tio, sp);
            (void)cfsetospeed(&tio, sp);
        }
        (void)tcsetattr(fd, TCSANOW, &tio);
    }
    return fd;
}

void
tiku_desk_conduct_shutdown(tiku_desk_conduct_t *c)
{
    if (c == NULL) {
        return;
    }
    if (c->peer >= 0) {
        (void)close(c->peer);
        c->peer = -1;
    }
    if (c->fd >= 0) {
        (void)close(c->fd);
        (void)unlink(c->path);
        c->fd = -1;
    }
    free(c->buf);
    c->buf = NULL;
    c->got = c->want = c->hgot = 0u;
}

/** @brief Serve one complete message.  @return nonzero if it injected. */
static int
conduct_message(tiku_desk_conduct_t *c)
{
    const unsigned char *p = c->buf;

    switch (c->cur_type) {
    case TIKU_DESK_CMSG_HELLO:
        /* A driver from another world is refused rather than half-served:
         * the version is the only thing checked before anything acts. */
        if (c->want >= 4u) {
            uint32_t version;

            memcpy(&version, p, 4);
            if (version != TIKU_DESK_CONDUCT_VERSION) {
                (void)close(c->peer);
                c->peer = -1;
            }
        }
        break;
    case TIKU_DESK_CMSG_INJECT:
        if (c->want >= sizeof(tiku_desk_event_t) && c->inject != NULL) {
            tiku_desk_event_t ev;

            memcpy(&ev, p, sizeof ev);
            c->inject(c->ctx, &ev);
            return 1;
        }
        break;
    case TIKU_DESK_CMSG_QUERY:
        if (c->want >= 12u + TIKU_DESK_CONDUCT_ARG) {
            uint32_t what;
            int32_t a, b;
            char arg[TIKU_DESK_CONDUCT_ARG];
            int32_t reply[2];
            char text[TIKU_DESK_CONDUCT_TEXT];
            unsigned char out[8 + TIKU_DESK_CONDUCT_TEXT];

            memcpy(&what, p, 4);
            memcpy(&a, p + 4, 4);
            memcpy(&b, p + 8, 4);
            memcpy(arg, p + 12, sizeof arg);
            arg[sizeof arg - 1] = '\0';
            reply[0] = 0;
            reply[1] = 0;
            text[0] = '\0';
            if (what == TIKU_DESK_CQ_PIXEL && c->pixel != NULL) {
                reply[1] = (int32_t)c->pixel(c->ctx, a, b);
                reply[0] = 1;
            } else if (what == TIKU_DESK_CQ_TEXT && c->text != NULL) {
                reply[0] = c->text(c->ctx, arg, text, sizeof text) ? 1 : 0;
            }
            memcpy(out, reply, sizeof reply);
            memcpy(out + 8, text, sizeof text);
            send_msg(c->peer, TIKU_DESK_CMSG_ANSWER, out, sizeof out);
        }
        break;
    default:
        break;
    }
    return 0;
}

int
tiku_desk_conduct_poll(tiku_desk_conduct_t *c)
{
    int injected = 0;

    if (c == NULL || (c->fd < 0 && c->peer < 0)) {
        return 0;
    }
    if (c->fd >= 0 && c->peer < 0) {
        int fd = accept(c->fd, NULL, NULL);

        if (fd >= 0) {
            (void)set_nonblock(fd);
            c->peer = fd;
            c->hgot = c->got = c->want = 0u;
        }
    }
    while (c->peer >= 0) {
        int r = stream_read(c->peer, c->hbuf, &c->hgot, &c->buf, &c->got,
                            &c->want, &c->cur_type);

        if (r < 0) {
            (void)close(c->peer);
            c->peer = -1;
            break;
        }
        if (r == 0) {
            break;
        }
        /* Handled BEFORE the length is cleared: every branch below
         * checks the payload size, and a zeroed one refuses them all. */
        /* Handled BEFORE the length is cleared: every branch below
         * checks the payload size, and a zeroed one refuses them all. */
        injected |= conduct_message(c);
        c->want = 0u;
    }
    return injected;
}

/*---------------------------------------------------------------------------*/
/* The driver's side                                                         */
/*---------------------------------------------------------------------------*/

int
tiku_desk_conduct_connect(tiku_desk_conduct_client_t *c, const char *path,
                          int wait_ms)
{
    struct sockaddr_un addr;
    int waited = 0;

    if (c == NULL || path == NULL) {
        return -1;
    }
    memset(c, 0, sizeof *c);
    c->fd = -1;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
    for (;;) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);

        if (fd >= 0) {
            if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
                uint32_t version = TIKU_DESK_CONDUCT_VERSION;

                /* Non-blocking here too, so the bounded wait for an
                 * answer can actually time out: a blocking read would
                 * sit in the kernel and never let the clock run. */
                (void)set_nonblock(fd);
                c->fd = fd;
                send_msg(fd, TIKU_DESK_CMSG_HELLO, &version,
                         sizeof version);
                return 0;
            }
            (void)close(fd);
        }
        /* The shell may still be starting: waiting here is what frees the
         * harness from having to sequence two processes by hand. */
        if (waited >= wait_ms) {
            return -1;
        }
        {
            struct timespec ts;

            ts.tv_sec = 0;
            ts.tv_nsec = 20 * 1000 * 1000;
            (void)nanosleep(&ts, NULL);
            waited += 20;
        }
    }
}

int
tiku_desk_conduct_connect_fd(tiku_desk_conduct_client_t *c, int fd)
{
    uint32_t version = TIKU_DESK_CONDUCT_VERSION;

    if (c == NULL || fd < 0) {
        return -1;
    }
    memset(c, 0, sizeof *c);
    (void)signal(SIGPIPE, SIG_IGN);
    (void)set_nonblock(fd);
    c->fd = fd;
    send_msg(fd, TIKU_DESK_CMSG_HELLO, &version, sizeof version);
    return 0;
}

void
tiku_desk_conduct_disconnect(tiku_desk_conduct_client_t *c)
{
    if (c != NULL && c->fd >= 0) {
        (void)close(c->fd);
        c->fd = -1;
    }
}

int
tiku_desk_conduct_inject(tiku_desk_conduct_client_t *c,
                         const tiku_desk_event_t *event)
{
    if (c == NULL || c->fd < 0 || event == NULL) {
        return -1;
    }
    send_msg(c->fd, TIKU_DESK_CMSG_INJECT, event, (uint32_t)sizeof *event);
    return 0;
}

int
tiku_desk_conduct_query(tiku_desk_conduct_client_t *c, uint32_t what,
                        int a, int b, const char *arg, int *num,
                        char *text, size_t max)
{
    unsigned char msg[12 + TIKU_DESK_CONDUCT_ARG];
    int32_t ab[2];
    unsigned char *buf = NULL;
    size_t got = 0u, want = 0u;
    uint32_t type = 0u;
    unsigned char hbuf[8];
    size_t hgot = 0u;
    int ok = 0;
    int waited;

    if (c == NULL || c->fd < 0) {
        return 0;
    }
    ab[0] = a;
    ab[1] = b;
    memcpy(msg, &what, 4);
    memcpy(msg + 4, ab, sizeof ab);
    memset(msg + 12, 0, TIKU_DESK_CONDUCT_ARG);
    if (arg != NULL) {
        snprintf((char *)msg + 12, TIKU_DESK_CONDUCT_ARG, "%s", arg);
    }
    send_msg(c->fd, TIKU_DESK_CMSG_QUERY, msg, (uint32_t)sizeof msg);

    /* Wait for the one answer, but NOT forever: a shell that stopped
     * answering must show up as a failed question, not as a driver that
     * never returns.  A harness that hangs is worse than one that fails,
     * because it reports nothing at all and someone waits on it. */
    for (waited = 0; waited < TIKU_DESK_CONDUCT_ANSWER_MS; ) {
        int r = stream_read(c->fd, hbuf, &hgot, &buf, &got, &want, &type);

        if (r < 0) {
            free(buf);
            return 0;
        }
        if (r == 0) {
            struct timespec ts;

            ts.tv_sec = 0;
            ts.tv_nsec = 2 * 1000 * 1000;
            (void)nanosleep(&ts, NULL);
            waited += 2;
            continue;
        }
        if (type == TIKU_DESK_CMSG_ANSWER &&
            want >= 8u + TIKU_DESK_CONDUCT_TEXT) {
            int32_t reply[2];

            memcpy(reply, buf, sizeof reply);
            ok = (reply[0] != 0);
            if (num != NULL) {
                *num = (int)reply[1];
            }
            if (text != NULL && max > 0u) {
                snprintf(text, max, "%s", (const char *)buf + 8);
            }
            free(buf);
            return ok;
        }
        want = 0u;              /* something else: keep waiting */
    }
    free(buf);
    return 0;                   /* it never answered */
}
