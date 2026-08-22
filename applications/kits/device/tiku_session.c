/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_session.c - prompt discipline, echo removal, notify demux.
 *
 * The cadence follows TikuBench's ShellSession, which is proven against every
 * board in the fleet: strip ANSI, match the cwd-tolerant prompt, and treat
 * anything the device says before it as the answer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_session.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* One BASIC LIST or a full manifest is the worst case the buffer must hold. */
#define SESSION_BUF_INIT  (16u * 1024u)
#define SESSION_BUF_MAX   (512u * 1024u)

struct tiku_session {
    tiku_tx_t     *tx;
    unsigned char      *buf;
    size_t              len;
    size_t              cap;
    tiku_notify_fn notify;
    void               *notify_ctx;
};

/** @brief Milliseconds on the monotonic clock. */
static long
now_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int
buf_reserve(tiku_session_t *s, size_t extra)
{
    size_t need = s->len + extra + 1u;
    unsigned char *p;

    if (need <= s->cap) {
        return 0;
    }
    if (need > SESSION_BUF_MAX) {
        return -1;
    }
    while (s->cap < need) {
        s->cap *= 2u;
    }
    p = realloc(s->buf, s->cap);
    if (p == NULL) {
        return -1;
    }
    s->buf = p;
    return 0;
}

/**
 * @brief Remove CSI escape sequences in place.
 *
 * The shell wraps its prompt in colour when built with TIKU_SHELL_COLOR=1;
 * stripping here lets one matcher serve both builds.
 */
static void
strip_ansi(unsigned char *b, size_t *len)
{
    size_t r = 0, w = 0;

    while (r < *len) {
        if (b[r] == 0x1bu && r + 1u < *len && b[r + 1u] == '[') {
            size_t k = r + 2u;
            while (k < *len && !((b[k] >= 'A' && b[k] <= 'Z') ||
                                 (b[k] >= 'a' && b[k] <= 'z'))) {
                k++;
            }
            if (k < *len) {
                r = k + 1u;            /* whole sequence consumed */
                continue;
            }
            break;                     /* truncated: keep for the next read */
        }
        b[w++] = b[r++];
    }
    /* Preserve any partial escape at the tail. */
    while (r < *len) {
        b[w++] = b[r++];
    }
    *len = w;
}

/**
 * @brief Find the prompt: "tikuOS> " or "tikuOS:<cwd>> ".
 *
 * @return Offset of its first byte, or -1.  @p end_out receives the offset
 *         just past it.
 */
static long
find_prompt(const unsigned char *b, size_t len, size_t *end_out)
{
    static const char pfx[] = "tikuOS";
    size_t i;

    for (i = 0; i + 8u <= len; i++) {
        size_t k;
        if (memcmp(b + i, pfx, 6u) != 0) {
            continue;
        }
        k = i + 6u;
        if (b[k] == ':') {
            while (k < len && b[k] != '>' && b[k] != '\n') {
                k++;
            }
        }
        if (k + 1u < len && b[k] == '>' && b[k + 1u] == ' ') {
            *end_out = k + 2u;
            return (long)i;
        }
    }
    return -1;
}

/**
 * @brief Pull complete `~<path>` lines out of the buffer and dispatch them.
 *
 * Subscription lines can land between any two bytes of a command's output, so
 * they are removed from the stream rather than parsed at the edges.
 *
 * @return Lines dispatched.
 */
static int
drain_notifies(tiku_session_t *s)
{
    size_t r = 0, w = 0;
    int fired = 0;
    int at_line_start = 1;

    while (r < s->len) {
        if (at_line_start && s->buf[r] == '~') {
            size_t e = r;
            while (e < s->len && s->buf[e] != '\n') {
                e++;
            }
            if (e >= s->len) {
                break;                 /* incomplete: leave it for next read */
            }
            if (s->notify != NULL) {
                char path[256];
                size_t n = e - r - 1u;
                if (n > 0u && s->buf[e - 1u] == '\r') {
                    n--;
                }
                if (n >= sizeof path) {
                    n = sizeof path - 1u;
                }
                memcpy(path, s->buf + r + 1u, n);
                path[n] = '\0';
                s->notify(path, s->notify_ctx);
            }
            fired++;
            r = e + 1u;
            at_line_start = 1;
            continue;
        }
        at_line_start = (s->buf[r] == '\n');
        s->buf[w++] = s->buf[r++];
    }
    /* Keep whatever tail was left unexamined. */
    while (r < s->len) {
        s->buf[w++] = s->buf[r++];
    }
    s->len = w;
    return fired;
}

/** @brief One transport read appended to the buffer, then normalised. */
static int
pump(tiku_session_t *s, int timeout_ms, int *notified)
{
    unsigned char chunk[4096];
    int n = tiku_tx_read(s->tx, chunk, sizeof chunk, timeout_ms);
    int f;

    if (n < 0) {
        return -1;
    }
    if (n > 0) {
        if (buf_reserve(s, (size_t)n) != 0) {
            return -1;
        }
        memcpy(s->buf + s->len, chunk, (size_t)n);
        s->len += (size_t)n;
        strip_ansi(s->buf, &s->len);
    }
    f = drain_notifies(s);
    if (notified != NULL) {
        *notified += f;
    }
    return n;
}

tiku_session_t *
tiku_session_new(tiku_tx_t *tx)
{
    tiku_session_t *s;

    if (tx == NULL) {
        return NULL;
    }
    s = calloc(1, sizeof *s);
    if (s == NULL) {
        return NULL;
    }
    s->buf = malloc(SESSION_BUF_INIT);
    if (s->buf == NULL) {
        free(s);
        return NULL;
    }
    s->cap = SESSION_BUF_INIT;
    s->tx  = tx;
    return s;
}

void
tiku_session_free(tiku_session_t *s)
{
    if (s == NULL) {
        return;
    }
    tiku_tx_close(s->tx);
    free(s->buf);
    free(s);
}

void
tiku_session_on_notify(tiku_session_t *s, tiku_notify_fn fn,
                            void *ctx)
{
    if (s != NULL) {
        s->notify = fn;
        s->notify_ctx = ctx;
    }
}

int
tiku_session_sync(tiku_session_t *s, int timeout_ms)
{
    long deadline;

    if (s == NULL) {
        return -1;
    }
    s->len = 0;
    if (tiku_tx_write(s->tx, "\r\n", 2u) < 0) {
        return -1;
    }
    deadline = now_ms() + timeout_ms;
    for (;;) {
        size_t end;
        if (find_prompt(s->buf, s->len, &end) >= 0) {
            s->len = 0;
            return 0;
        }
        if (now_ms() >= deadline) {
            return -1;
        }
        if (pump(s, 50, NULL) < 0) {
            return -1;
        }
    }
}

int
tiku_session_cmd(tiku_session_t *s, const char *line, char *out,
                      size_t max, int timeout_ms)
{
    long deadline;
    size_t echo;

    if (s == NULL || line == NULL) {
        return -1;
    }
    /* Discard anything still in flight (a second prompt from sync, a late
     * banner) so this command's frame starts at a known point. */
    s->len = 0;
    while (pump(s, 5, NULL) > 0) {
        s->len = 0;
    }
    if (tiku_tx_write(s->tx, line, strlen(line)) < 0 ||
        tiku_tx_write(s->tx, "\n", 1u) < 0) {
        return -1;
    }
    echo = strlen(line);
    deadline = now_ms() + timeout_ms;

    for (;;) {
        size_t end;
        long at = find_prompt(s->buf, s->len, &end);

        if (at >= 0) {
            size_t start = 0, n, eol = 0;

            /* The shell echoes the typed line.  Match it at the END of the
             * first line rather than the start: a stale prompt fragment can
             * precede the echo, and dropping through the newline removes
             * both in one step. */
            while (eol < (size_t)at && s->buf[eol] != '\n') {
                eol++;
            }
            if (eol <= (size_t)at && echo > 0u) {
                size_t line_len = eol;
                while (line_len > 0u && s->buf[line_len - 1u] == '\r') {
                    line_len--;
                }
                if (line_len >= echo &&
                    memcmp(s->buf + line_len - echo, line, echo) == 0) {
                    start = (eol < (size_t)at) ? eol + 1u : (size_t)at;
                }
            }
            n = (size_t)at - start;
            /* Trim the blank line the shell prints before its prompt. */
            while (n > 0u && (s->buf[start + n - 1u] == '\n' ||
                              s->buf[start + n - 1u] == '\r')) {
                n--;
            }
            if (out != NULL && max > 0u) {
                size_t cp = (n < max - 1u) ? n : max - 1u;
                memcpy(out, s->buf + start, cp);
                out[cp] = '\0';
                n = cp;
            }
            /* Anything after the prompt belongs to the next read. */
            memmove(s->buf, s->buf + end, s->len - end);
            s->len -= end;
            return (int)n;
        }
        if (now_ms() >= deadline) {
            if (out != NULL && max > 0u) {
                out[0] = '\0';
            }
            return -1;
        }
        if (pump(s, 50, NULL) < 0) {
            return -1;
        }
    }
}

int
tiku_session_poll(tiku_session_t *s, int timeout_ms)
{
    int fired = 0;

    if (s == NULL) {
        return -1;
    }
    if (pump(s, timeout_ms, &fired) < 0) {
        return -1;
    }
    return fired;
}

int
tiku_session_reconnect(tiku_session_t *s)
{
    if (s == NULL || tiku_tx_reopen(s->tx) != 0) {
        return -1;
    }
    s->len = 0;
    return tiku_session_sync(s, 4000);
}

const char *
tiku_session_name(const tiku_session_t *s)
{
    return (s != NULL) ? tiku_tx_name(s->tx) : "(none)";
}
