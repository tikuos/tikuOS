/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_tx.c - serial and TCP transports.
 *
 * Serial is raw 8N1 with no flow control and DTR left asserted; TCP is a plain
 * stream to the telnet-shell port.  Both poll with a deadline so the UI thread
 * never blocks on a silent device.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desk_tx.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <stdio.h>

#define TX_SERIAL 0
#define TX_TCP    1

struct tiku_desk_tx {
    int  kind;
    int  fd;
    int  baud;                 /* serial only */
    int  port;                 /* tcp only    */
    char endpoint[256];        /* device path or host */
    char name[288];
};

/** @brief Map an integer rate onto its termios constant; B0 if unsupported. */
static speed_t
tx_speed(int baud)
{
    switch (baud) {
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default:     return B0;
    }
}

/** @brief Configure an open fd as raw 8N1 at @p baud.  0 on success. */
static int
tx_configure_serial(int fd, int baud)
{
    struct termios t;
    speed_t sp = tx_speed(baud);

    if (sp == B0 || tcgetattr(fd, &t) != 0) {
        errno = EINVAL;
        return -1;
    }
    cfmakeraw(&t);
    t.c_cflag |= (CLOCAL | CREAD);
    t.c_cflag &= (tcflag_t)~CRTSCTS;
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;
    (void)cfsetispeed(&t, sp);
    (void)cfsetospeed(&t, sp);
    if (tcsetattr(fd, TCSANOW, &t) != 0) {
        return -1;
    }
    (void)tcflush(fd, TCIOFLUSH);
    return 0;
}

tiku_desk_tx_t *
tiku_desk_tx_open_serial(const char *path, int baud)
{
    tiku_desk_tx_t *tx;
    int fd;

    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return NULL;
    }
    if (tx_configure_serial(fd, baud) != 0) {
        (void)close(fd);
        return NULL;
    }
    tx = calloc(1, sizeof *tx);
    if (tx == NULL) {
        (void)close(fd);
        return NULL;
    }
    tx->kind = TX_SERIAL;
    tx->fd   = fd;
    tx->baud = baud;
    snprintf(tx->endpoint, sizeof tx->endpoint, "%s", path);
    snprintf(tx->name, sizeof tx->name, "%s @%d", path, baud);
    return tx;
}

/** @brief Connect a stream socket to host:port.  Returns the fd or -1. */
static int
tx_dial(const char *host, int port)
{
    struct addrinfo hints, *res = NULL, *ai;
    char svc[16];
    int fd = -1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(svc, sizeof svc, "%d", port);
    if (getaddrinfo(host, svc, &hints, &res) != 0) {
        return -1;
    }
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        (void)close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd >= 0) {
        int one = 1;
        /* The shell's answers are small and latency-visible in the UI. */
        (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        (void)fcntl(fd, F_SETFL, O_NONBLOCK);
    }
    return fd;
}

tiku_desk_tx_t *
tiku_desk_tx_open_tcp(const char *host, int port)
{
    tiku_desk_tx_t *tx;
    int fd = tx_dial(host, port);

    if (fd < 0) {
        return NULL;
    }
    tx = calloc(1, sizeof *tx);
    if (tx == NULL) {
        (void)close(fd);
        return NULL;
    }
    tx->kind = TX_TCP;
    tx->fd   = fd;
    tx->port = port;
    snprintf(tx->endpoint, sizeof tx->endpoint, "%s", host);
    snprintf(tx->name, sizeof tx->name, "%s:%d", host, port);
    return tx;
}

int
tiku_desk_tx_read(tiku_desk_tx_t *tx, void *buf, size_t max, int timeout_ms)
{
    struct pollfd p;
    ssize_t n;

    if (tx == NULL || tx->fd < 0) {
        return -1;
    }
    p.fd = tx->fd;
    p.events = POLLIN;
    p.revents = 0;
    if (poll(&p, 1, timeout_ms) <= 0) {
        return 0;                      /* timeout, or a signal: no data yet */
    }
    n = read(tx->fd, buf, max);
    if (n > 0) {
        return (int)n;
    }
    /* 0 from a socket is a closed peer; on a serial fd it is just quiet. */
    if (n == 0) {
        return (tx->kind == TX_TCP) ? -1 : 0;
    }
    return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
}

int
tiku_desk_tx_write(tiku_desk_tx_t *tx, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    size_t off = 0;

    if (tx == NULL || tx->fd < 0) {
        return -1;
    }
    while (off < len) {
        ssize_t n = write(tx->fd, p + off, len - off);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            struct pollfd pw = { tx->fd, POLLOUT, 0 };
            if (poll(&pw, 1, 1000) <= 0) {
                return -1;
            }
            continue;
        }
        return -1;
    }
    return (int)off;
}

int
tiku_desk_tx_reopen(tiku_desk_tx_t *tx)
{
    if (tx == NULL) {
        return -1;
    }
    if (tx->fd >= 0) {
        (void)close(tx->fd);
        tx->fd = -1;
    }
    if (tx->kind == TX_SERIAL) {
        int fd = open(tx->endpoint, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) {
            return -1;
        }
        if (tx_configure_serial(fd, tx->baud) != 0) {
            (void)close(fd);
            return -1;
        }
        tx->fd = fd;
        return 0;
    }
    tx->fd = tx_dial(tx->endpoint, tx->port);
    return (tx->fd >= 0) ? 0 : -1;
}

const char *
tiku_desk_tx_name(const tiku_desk_tx_t *tx)
{
    return (tx != NULL) ? tx->name : "(none)";
}

void
tiku_desk_tx_close(tiku_desk_tx_t *tx)
{
    if (tx == NULL) {
        return;
    }
    if (tx->fd >= 0) {
        (void)close(tx->fd);
    }
    free(tx);
}

int
tiku_desk_tx_scan_serial(char out[][256], int max)
{
    static const char *dir = "/dev/serial/by-id";
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;

    if (d == NULL) {
        return -1;
    }
    while (n < max && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        /* by-id names carry the probe: J-Link (Nordic/Ambiq/Renesas kits),
         * CDC (Pico), FTDI (the FR5994 rig). */
        if (strstr(e->d_name, "J-Link") == NULL &&
            strstr(e->d_name, "CDC")    == NULL &&
            strstr(e->d_name, "FT232")  == NULL &&
            strstr(e->d_name, "Pico")   == NULL) {
            continue;
        }
        /* by-id names run long; a truncated path would open the wrong
         * device or nothing, so drop the entry instead. */
        if (strlen(dir) + strlen(e->d_name) + 2u > 256u) {
            continue;
        }
        (void)snprintf(out[n], 256, "%s/%s", dir, e->d_name);
        n++;
    }
    (void)closedir(d);
    return n;
}
