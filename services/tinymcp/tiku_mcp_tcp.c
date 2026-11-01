/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_tcp.c - MCP on TCP: one connection of newline-delimited lines,
 * Streamable HTTP connections beside it, each one's state leased while it
 * is open, and /sys/mcp/key.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mcp_tcp.h"

#include <stdio.h>
#include <string.h>

#include "tiku.h"
#include "tiku_mcp.h"
#include "tiku_mcp_ask.h"
#include <kernel/memory/tiku_mem.h>
#if TIKU_MCP_TLS_ENABLE
#include "tiku_mcp_tls.h"
#endif
#include <kernel/vfs/tiku_vfs.h>
#include <tinymcp_http.h>

/*---------------------------------------------------------------------------*/
/* THE KEY                                                                   */
/*---------------------------------------------------------------------------*/

#define KEY_MAGIC 0x4D43504BUL     /* 'MCPK' */

/* The owner's key, empty for none; never read back through a node. */
static TIKU_DURABLE char key_persist[TIKU_MCP_KEY_MAX + 1];
TIKU_PERSIST_CELL(key_cell, key_persist, KEY_MAGIC, NULL, 0);

void
tiku_mcp_tcp_init(void)
{
    (void)tiku_persist_cell_init(&key_cell);
#if TIKU_MCP_TLS_ENABLE
    tiku_mcp_tls_init();
#endif
}

/** @brief Whether the owner has set a key. */
static int
key_set(void)
{
    return key_persist[0] != 0;
}

int
tiku_mcp_key_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", key_set() ? "set" : "none");
}

/**
 * A write sets the key, TIKU_MCP_KEY_MIN to TIKU_MCP_KEY_MAX printable
 * characters without spaces; "none" clears it.
 */
int
tiku_mcp_key_write(const char *buf, size_t len)
{
    char key[TIKU_MCP_KEY_MAX + 1];
    size_t i;

    if (len > 0u && buf[len - 1u] == '\n') {
        len--;
    }
    if (len == 4u && memcmp(buf, "none", 4u) == 0) {
        len = 0u;
    } else if (len < TIKU_MCP_KEY_MIN || len > TIKU_MCP_KEY_MAX) {
        return TIKU_VFS_EINVAL;
    }
    for (i = 0u; i < len; i++) {
        if (buf[i] <= ' ' || buf[i] > '~') {
            return TIKU_VFS_EINVAL;
        }
    }
    memset(key, 0, sizeof key);
    memcpy(key, buf, len);
    tiku_persist_cell_commit(&key_cell, key, (uint16_t)sizeof key);
    tiku_mcp_journal(tiku_mcp_actor(), len ? "set the key" : "cleared the key");
    return 0;
}

/** @brief @p sent equals the key, compared byte by byte to the end. */
static int
key_equals(const char *sent, size_t n)
{
    uint8_t diff = 0u;
    size_t i;

    if (!key_set() || n != strlen(key_persist)) {
        return 0;
    }
    for (i = 0u; i < n; i++) {
        diff |= (uint8_t)(sent[i] ^ key_persist[i]);
    }
    return diff == 0u;
}

#if TIKU_MCP_TCP_ENABLE

#include <kernel/cpu/tiku_watchdog.h>
#include <kernel/timers/tiku_clock.h>
#include <services/console/tiku_console.h>
#include <tikukits/net/tiku_kits_net.h>
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>
#if defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_psram_arch.h>
#endif
#if (TIKU_DRV_WIFI_CYW43_ENABLE + 0) || (TIKU_DRV_WIFI_ESP_ENABLE + 0)
#include <interfaces/wireless/tiku_wireless.h>
#define MCP_TCP_WIFI 1
#endif
#if TIKU_MCP_TLS_ENABLE
#define TLS_HEAD TIKU_KITS_CRYPTO_TLS_SERVER_HEAD
#define TLS_OVER TIKU_KITS_CRYPTO_TLS_SERVER_OVERHEAD
#endif

/*---------------------------------------------------------------------------*/
/* THE CONNECTIONS                                                           */
/*---------------------------------------------------------------------------*/

#define PEND_MAX 256u              /* bytes read past an HTTP request owed */

/* What one connection holds while it is open, all of it in its arena. */
typedef struct {
    tinymcp_t      t;
    tinymcp_http_t h;
    uint16_t       staged;                     /* bytes in stage */
    uint16_t       npend;                      /* bytes in pend */
    uint8_t        stage[TIKU_KITS_NET_TCP_MSS];
    char           pend[PEND_MAX];
    char           in[TIKU_MCP_IN_CAP];
    char           out[TIKU_MCP_OUT_CAP];
} conn_mem_t;

#if TIKU_MCP_TLS_ENABLE
/* An HTTPS connection's TLS state and record buffer, after its conn_mem_t
 * in the same arena. */
typedef struct {
    tiku_kits_crypto_tls_server_t s;
    uint8_t                       rec[TIKU_MCP_TLS_REC];
} tls_mem_t;
#endif

typedef struct {
    tiku_kits_net_tcp_conn_t *tcb;    /* the TCB, NULL once it went */
    conn_mem_t               *m;      /* in arena while open, else NULL */
    tiku_arena_t              arena;
    uint8_t                   http;   /* Streamable HTTP, not lines */
#if TIKU_MCP_TLS_ENABLE
    tls_mem_t                *tls;    /* HTTPS: in the arena; NULL for HTTP */
#endif
} slot_t;

static slot_t      slots[TIKU_MCP_TCP_SLOTS];  /* [0] lines, the rest HTTP */
static uint16_t    line_port, http_port;       /* listening on, 0: not */
#if TIKU_MCP_TLS_ENABLE
static uint16_t    https_port;
#endif
static uint32_t    accepted, refused;
static const char *why = "";                    /* the last refusal's reason */

static slot_t *
slot_of(const tinymcp_t *s)
{
    unsigned i;

    for (i = 0u; i < TIKU_MCP_TCP_SLOTS; i++) {
        if (slots[i].m != (conn_mem_t *)0 && &slots[i].m->t == s) {
            return &slots[i];
        }
    }
    return (slot_t *)0;
}

int
tiku_mcp_tcp_owns(const tinymcp_t *stream)
{
    return slot_of(stream) != (slot_t *)0;
}

tinymcp_t *
tiku_mcp_tcp_stream(unsigned i)
{
    return i < TIKU_MCP_TCP_SLOTS && slots[i].m != (conn_mem_t *)0
               ? &slots[i].m->t : (tinymcp_t *)0;
}

int
tiku_mcp_tcp_keyed(tinymcp_t *stream)
{
    slot_t *s = slot_of(stream);
    char sent[TIKU_MCP_KEY_MAX + 1];
    int n;

    if (s == (slot_t *)0) {
        return 0;
    }
#if TIKU_MCP_TLS_ENABLE
    /* Once the board can serve HTTPS, the key crosses the network only
     * inside TLS. */
    if (s->tls == (tls_mem_t *)0 && tiku_mcp_tls_cred() != (const void *)0) {
        return 0;
    }
#endif
    if (s->http) {
        const char *b = tinymcp_http_bearer(&s->m->h);

        if (key_equals(b, strlen(b))) {
            return 1;
        }
    }
    n = tinymcp_meta(stream, TIKU_MCP_KEY_META, sent, sizeof sent);
    return n >= 0 && key_equals(sent, (size_t)n);
}

/**
 * @brief Release a connection that went: its subscriptions, kept calls and
 *        state.  Only from the pump, where no request is being served, so
 *        the library never writes into memory already given back.
 */
static void
release(slot_t *s)
{
    if (s->m == (conn_mem_t *)0 || s->tcb != (tiku_kits_net_tcp_conn_t *)0) {
        return;
    }
    tinymcp_closed(&s->m->t);
    s->m = (conn_mem_t *)0;
    /* It held requests that carried the key: zeroed, then given back. */
    (void)tiku_arena_secure_reset(&s->arena);
    (void)tiku_arena_destroy(&s->arena);
}

static void
tcp_recv(tiku_kits_net_tcp_conn_t *c, uint16_t available)
{
    (void)c;
    (void)available;            /* the pump reads it */
}

static int http_write(void *ctx, const char *bytes, size_t n);
#if TIKU_MCP_TLS_ENABLE
static int tls_out(void *ctx, const uint8_t *bytes, size_t n);
#endif

/** @brief A connection made: a free slot of its kind, or refused. */
static void
accept_conn(tiku_kits_net_tcp_conn_t *c)
{
    int http = c->local_port == http_port && http_port != 0u;
    int tls = 0;
    unsigned first, last;
    size_t need = sizeof(conn_mem_t);
    tiku_mem_request_t lease = TIKU_MEM_REQUEST_DEFAULT;
    slot_t *s = (slot_t *)0;
    unsigned i;

#if TIKU_MCP_TLS_ENABLE
    if (c->local_port == https_port && https_port != 0u) {
        if (tiku_mcp_tls_cred() == (const void *)0) {
            tiku_kits_net_tcp_abort(c);
            refused++;
            why = "no certificate";
            return;
        }
        http = tls = 1;
        need += sizeof(tls_mem_t);
    }
#endif
    first = http ? 1u : 0u;
    last = http ? TIKU_MCP_TCP_SLOTS : 1u;

    for (i = first; i < last && s == (slot_t *)0; i++) {
        if (slots[i].m == (conn_mem_t *)0) {
            s = &slots[i];
        }
    }
    if (s == (slot_t *)0) {
        tiku_kits_net_tcp_abort(c);
        refused++;
        why = "busy";
        return;
    }
    /* SRAM while the connection fits there, external memory once it is
     * short; the ESP32-C61 attaches its PSRAM on first need. */
#if defined(PLATFORM_ESP32C61)
    (void)tiku_esp32c61_psram_attach();
#endif
    lease.flags = TIKU_MEM_ALLOW_EXTERNAL;
    if (tiku_mem_arena_create(&s->arena, need + 16u, &lease) != TIKU_MEM_OK ||
        (s->m = tiku_arena_alloc(&s->arena, sizeof(conn_mem_t))) ==
            (conn_mem_t *)0) {
        (void)tiku_arena_reset(&s->arena);
        (void)tiku_arena_destroy(&s->arena);
        s->m = (conn_mem_t *)0;
        tiku_kits_net_tcp_abort(c);
        refused++;
        why = "no memory";
        return;
    }
    s->tcb = c;
    s->http = (uint8_t)http;
#if TIKU_MCP_TLS_ENABLE
    s->tls = (tls_mem_t *)0;
    if (tls) {
        s->tls = tiku_arena_alloc(&s->arena, sizeof(tls_mem_t));
        if (s->tls == (tls_mem_t *)0 ||
            tiku_kits_crypto_tls_server_init(&s->tls->s, tiku_mcp_tls_cred(),
                                             tiku_mcp_tls_rng, tls_out, s,
                                             s->tls->rec,
                                             (uint16_t)sizeof s->tls->rec) != 0) {
            (void)tiku_arena_reset(&s->arena);
            (void)tiku_arena_destroy(&s->arena);
            s->m = (conn_mem_t *)0;
            s->tcb = (tiku_kits_net_tcp_conn_t *)0;
            tiku_kits_net_tcp_abort(c);
            refused++;
            why = "no memory";
            return;
        }
    }
#else
    (void)tls;
#endif
    s->m->staged = 0u;
    s->m->npend = 0u;
    tinymcp_init(&s->m->t, &tiku_mcp_host, &s->m->t, s->m->in,
                 sizeof s->m->in, s->m->out, sizeof s->m->out, TIKU_MCP_SUBS);
    if (http) {
        tinymcp_http_init(&s->m->h, &s->m->t, "/mcp", http_write, s);
    }
    accepted++;
}

static void
tcp_event(tiku_kits_net_tcp_conn_t *c, uint8_t event)
{
    unsigned i;

    if (event == TIKU_KITS_NET_TCP_EVT_CONNECTED) {
        accept_conn(c);
        return;
    }
    for (i = 0u; i < TIKU_MCP_TCP_SLOTS; i++) {
        if (slots[i].tcb == c) {
            slots[i].tcb = (tiku_kits_net_tcp_conn_t *)0;  /* pump releases */
        }
    }
}

int
tiku_mcp_tcp_listen(uint16_t p, int kind)
{
    uint16_t *port = kind == TIKU_MCP_TCP_HTTP ? &http_port : &line_port;

#if TIKU_MCP_TLS_ENABLE
    if (kind == TIKU_MCP_TCP_HTTPS) {
        port = &https_port;
    }
#else
    if (kind == TIKU_MCP_TCP_HTTPS) {
        return p == 0u ? 0 : -1;
    }
#endif
    if (*port != 0u) {
        (void)tiku_kits_net_tcp_unlisten(*port);
        *port = 0u;
    }
    if (p == 0u) {
        return 0;
    }
    tiku_kits_net_tcp_init();
    if (p == line_port || p == http_port ||
#if TIKU_MCP_TLS_ENABLE
        p == https_port ||
#endif
        tiku_kits_net_tcp_listen(p, tcp_recv, tcp_event) !=
            TIKU_KITS_NET_OK) {
        return -1;
    }
#if TIKU_MCP_TLS_ENABLE
    if (kind == TIKU_MCP_TCP_HTTPS) {
        tiku_mcp_tls_seed();       /* no client waits on the TRNG yet */
    }
#endif
    *port = p;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* SENDING                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief The stack's timers, about 20 times a second: retransmission,
 *        TIME_WAIT and the reaper of half-closed connections.  The net
 *        test build's shell runs them itself.
 */
static void
timers(void)
{
#if !TIKU_SHELL_NET_TEST
    static tiku_clock_time_t last;
    tiku_clock_time_t now = tiku_clock_time();

    if ((tiku_clock_time_t)(now - last) >=
        (tiku_clock_time_t)(TIKU_CLOCK_SECOND / 20)) {
        last = now;
        tiku_kits_net_tcp_periodic();
    }
#endif
}

/**
 * @brief The first @p len bytes of the stage as one segment.  While the TX
 *        pool is full the console and the radio are polled and the stack's
 *        timers run, so the peer's acknowledgements come in and a lost
 *        segment goes again.
 * @return 0, or -1 when the connection went or no segment came free within
 *         TIKU_MCP_TCP_SEND_MS, in which case it is aborted
 */
static int
send_stage(slot_t *s, uint16_t len)
{
    tiku_clock_time_t start = tiku_clock_time();

    for (;;) {
        if (s->tcb == (tiku_kits_net_tcp_conn_t *)0) {
            return -1;
        }
        if (tiku_kits_net_tcp_send(s->tcb, s->m->stage, len) ==
            TIKU_KITS_NET_OK) {
            return 0;
        }
        if ((tiku_clock_time_t)(tiku_clock_time() - start) >=
            TIKU_CLOCK_MS_TO_TICKS(TIKU_MCP_TCP_SEND_MS)) {
            tiku_kits_net_tcp_abort(s->tcb);
            s->tcb = (tiku_kits_net_tcp_conn_t *)0;   /* the pump releases */
            return -1;
        }
        tiku_watchdog_kick();
        tiku_console_pump();
#if defined(MCP_TCP_WIFI)
        (void)tiku_wireless_rx_poll();
#endif
        timers();
    }
}

/**
 * @brief The staged bytes as one segment, sealed as one TLS record on an
 *        HTTPS connection.
 * @return 0, or -1 when the connection went (see send_stage())
 */
static int
flush_stage(slot_t *s)
{
    uint16_t len = s->m->staged;

    if (len == 0u) {
        return 0;
    }
    s->m->staged = 0u;
#if TIKU_MCP_TLS_ENABLE
    if (s->tls != (tls_mem_t *)0) {
        int r = tiku_kits_crypto_tls_server_seal(&s->tls->s, s->m->stage, len);

        if (r < 0) {
            return -1;
        }
        len = (uint16_t)r;
    }
#endif
    return send_stage(s, len);
}

/**
 * @brief @p len bytes into the stage, a segment sent each time it fills;
 *        on HTTPS the stage holds a record's plaintext after its header.
 */
static int
put(slot_t *s, const char *p, size_t len)
{
    uint16_t head = 0u, over = 0u;

#if TIKU_MCP_TLS_ENABLE
    if (s->tls != (tls_mem_t *)0) {
        head = TLS_HEAD;
        over = TLS_OVER;
    }
#endif
    while (len > 0u) {
        uint16_t room, take;

        if (s->tcb == (tiku_kits_net_tcp_conn_t *)0) {
            return -1;
        }
        room = (uint16_t)((s->tcb->snd_mss < sizeof s->m->stage
                               ? s->tcb->snd_mss : (uint16_t)sizeof s->m->stage) -
                          over);
#if TIKU_MCP_TLS_ENABLE
        if (s->tls != (tls_mem_t *)0 &&
            room > tiku_kits_crypto_tls_server_room(&s->tls->s)) {
            room = tiku_kits_crypto_tls_server_room(&s->tls->s);
        }
#endif
        take = (uint16_t)(len < (size_t)(room - s->m->staged)
                              ? len : (size_t)(room - s->m->staged));
        memcpy(s->m->stage + head + s->m->staged, p, take);
        s->m->staged = (uint16_t)(s->m->staged + take);
        p += take;
        len -= take;
        if (s->m->staged == room && flush_stage(s) != 0) {
            return -1;
        }
    }
    return 0;
}

#if TIKU_MCP_TLS_ENABLE
/**
 * @brief The TLS layer's records, as they are, a segment at a time.  The
 *        pump empties the stage before the layer runs, so nothing staged is
 *        overwritten or sent after a record sealed later.
 */
static int
tls_out(void *ctx, const uint8_t *bytes, size_t n)
{
    slot_t *s = ctx;

    if (s->m->staged != 0u) {
        return -1;
    }
    while (n > 0u) {
        uint16_t room, take;

        if (s->tcb == (tiku_kits_net_tcp_conn_t *)0) {
            return -1;
        }
        room = s->tcb->snd_mss < sizeof s->m->stage
                   ? s->tcb->snd_mss : (uint16_t)sizeof s->m->stage;
        take = (uint16_t)(n < room ? n : room);
        memcpy(s->m->stage, bytes, take);
        if (send_stage(s, take) != 0) {
            return -1;
        }
        bytes += take;
        n -= take;
    }
    return 0;
}
#endif

/** @brief The HTTP layer's bytes; NULL ends a response or an event. */
static int
http_write(void *ctx, const char *bytes, size_t n)
{
    slot_t *s = ctx;

    if (bytes == (const char *)0) {
        return flush_stage(s);
    }
    return put(s, bytes, n);
}

int
tiku_mcp_tcp_send_part(tinymcp_t *stream, const char *bytes, size_t len,
                       int last)
{
    slot_t *s = slot_of(stream);

    if (s == (slot_t *)0) {
        return -1;
    }
    if (s->http) {
        return tinymcp_http_send_part(&s->m->h, bytes, len, last);
    }
    if (put(s, bytes, len) != 0) {
        return -1;
    }
    if (!last) {
        return 0;
    }
    return put(s, "\n", 1u) == 0 ? flush_stage(s) : -1;
}

int
tiku_mcp_tcp_send(tinymcp_t *stream, const char *msg, size_t len)
{
    slot_t *s = slot_of(stream);

    if (s != (slot_t *)0 && s->http) {
        return tinymcp_http_send(&s->m->h, msg, len);
    }
    return tiku_mcp_tcp_send_part(stream, msg, len, 1);
}

/*---------------------------------------------------------------------------*/
/* THE PUMP                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief A line connection's requests, as they come. */
static void
pump_lines(slot_t *s)
{
    uint8_t chunk[128];
    uint16_t n;

    while (s->tcb != (tiku_kits_net_tcp_conn_t *)0 &&
           (n = tiku_kits_net_tcp_read(s->tcb, chunk, sizeof chunk)) > 0u) {
        tiku_mcp_identity();
        (void)tinymcp_feed(&s->m->t, chunk, n);
    }
}

/**
 * @brief An HTTP connection's requests: read while there is room, fed as
 *        far as the layer takes them, closed when it says so.
 */
static void
pump_http(slot_t *s)
{
    conn_mem_t *m = s->m;
    size_t taken;

    if (s->tcb != (tiku_kits_net_tcp_conn_t *)0 && m->npend < PEND_MAX) {
        m->npend = (uint16_t)(m->npend +
                   tiku_kits_net_tcp_read(s->tcb, (uint8_t *)m->pend + m->npend,
                                          (uint16_t)(PEND_MAX - m->npend)));
    }
    if (m->npend == 0u || s->tcb == (tiku_kits_net_tcp_conn_t *)0) {
        return;
    }
    tiku_mcp_identity();
    taken = tinymcp_http_feed(&m->h, m->pend, m->npend);
    if (s->m != m) {
        return;
    }
    memmove(m->pend, m->pend + taken, m->npend - taken);
    m->npend = (uint16_t)(m->npend - taken);
    if (m->h.state == TINYMCP_HTTP_S_CLOSE &&
        s->tcb != (tiku_kits_net_tcp_conn_t *)0) {
        (void)tiku_kits_net_tcp_close(s->tcb);
        s->tcb = (tiku_kits_net_tcp_conn_t *)0;
    }
}

#if TIKU_MCP_TLS_ENABLE
/**
 * @brief An HTTPS connection: bytes from TCP through TLS, its plaintext
 *        into the HTTP layer, while either moves; closed with close_notify
 *        when the layer or the client ends it.
 */
static void
pump_https(slot_t *s)
{
    conn_mem_t *m = s->m;
    tiku_kits_crypto_tls_server_t *t = &s->tls->s;
    int moved;

    if (flush_stage(s) != 0) {
        return;
    }
    do {
        const uint8_t *d;
        size_t n, used;

        moved = 0;
        if (s->tcb != (tiku_kits_net_tcp_conn_t *)0 && m->npend < PEND_MAX) {
            m->npend = (uint16_t)(m->npend +
                       tiku_kits_net_tcp_read(s->tcb, (uint8_t *)m->pend + m->npend,
                                              (uint16_t)(PEND_MAX - m->npend)));
        }
        if (m->npend > 0u && s->tcb != (tiku_kits_net_tcp_conn_t *)0) {
            used = tiku_kits_crypto_tls_server_feed(t, (const uint8_t *)m->pend,
                                                    m->npend);
            memmove(m->pend, m->pend + used, m->npend - used);
            m->npend = (uint16_t)(m->npend - used);
            moved = used > 0u;
        }
        d = tiku_kits_crypto_tls_server_data(t, &n);
        if (n > 0u && s->tcb != (tiku_kits_net_tcp_conn_t *)0) {
            tiku_mcp_identity();
            used = tinymcp_http_feed(&m->h, (const char *)d, n);
            if (s->m != m) {
                return;
            }
            tiku_kits_crypto_tls_server_take(t, used);
            moved |= used > 0u;
        }
    } while (moved && s->tcb != (tiku_kits_net_tcp_conn_t *)0);
    if (s->tcb != (tiku_kits_net_tcp_conn_t *)0 &&
        (t->state >= TIKU_KITS_CRYPTO_TLS_SERVER_CLOSED ||
         m->h.state == TINYMCP_HTTP_S_CLOSE)) {
        if (t->state != TIKU_KITS_CRYPTO_TLS_SERVER_FAILED &&
            flush_stage(s) == 0) {
            tiku_kits_crypto_tls_server_close(t);
        }
        if (s->tcb != (tiku_kits_net_tcp_conn_t *)0) {
            (void)tiku_kits_net_tcp_close(s->tcb);
            s->tcb = (tiku_kits_net_tcp_conn_t *)0;
        }
    }
}

/** @brief 1 while some HTTPS connection is between ClientHello and open. */
static int
handshaking(void)
{
    unsigned i;

    for (i = 1u; i < TIKU_MCP_TCP_SLOTS; i++) {
        if (slots[i].m != (conn_mem_t *)0 && slots[i].tls != (tls_mem_t *)0 &&
            slots[i].tls->s.state < TIKU_KITS_CRYPTO_TLS_SERVER_OPEN) {
            return 1;
        }
    }
    return 0;
}
#endif

void
tiku_mcp_tcp_pump(void)
{
    unsigned i;

#if TIKU_MCP_TLS_ENABLE
    if (https_port != 0u && !handshaking()) {
        tiku_mcp_tls_seed();
    }
    if (line_port == 0u && http_port == 0u && https_port == 0u) {
#else
    if (line_port == 0u && http_port == 0u) {
#endif
        for (i = 0u; i < TIKU_MCP_TCP_SLOTS && slots[i].m == (conn_mem_t *)0;
             i++) {
        }
        if (i == TIKU_MCP_TCP_SLOTS) {
            return;
        }
    }
    timers();
    for (i = 0u; i < TIKU_MCP_TCP_SLOTS; i++) {
        slot_t *s = &slots[i];

        if (s->m == (conn_mem_t *)0) {
            continue;
        }
        /* The client closed: its kept calls and listens end with it. */
        if (s->tcb != (tiku_kits_net_tcp_conn_t *)0 &&
            s->tcb->state == TIKU_KITS_NET_TCP_STATE_CLOSE_WAIT) {
            (void)tiku_kits_net_tcp_close(s->tcb);
            s->tcb = (tiku_kits_net_tcp_conn_t *)0;
        }
#if TIKU_MCP_TLS_ENABLE
        if (s->tls != (tls_mem_t *)0) {
            pump_https(s);
        } else
#endif
        if (s->http) {
            pump_http(s);
        } else {
            pump_lines(s);
        }
        release(s);
    }
}

int
tiku_mcp_tcp_read(char *buf, size_t max)
{
    size_t at = 0u;
    unsigned i, open = 0u;
    int n;

    if (slots[0].m != (conn_mem_t *)0 &&
        slots[0].tcb != (tiku_kits_net_tcp_conn_t *)0) {
        const uint8_t *a = slots[0].tcb->remote_addr.b;

        n = snprintf(buf, max, "lines %u, connected %u.%u.%u.%u:%u\n",
                     (unsigned)line_port, a[0], a[1], a[2], a[3],
                     (unsigned)slots[0].tcb->remote_port);
    } else if (line_port != 0u) {
        n = snprintf(buf, max, "lines %u\n", (unsigned)line_port);
    } else {
        n = snprintf(buf, max, "lines off\n");
    }
    if (n < 0 || (size_t)n >= max) {
        return n;
    }
    at = (size_t)n;
    for (i = 1u; i < TIKU_MCP_TCP_SLOTS; i++) {
        if (slots[i].m != (conn_mem_t *)0
#if TIKU_MCP_TLS_ENABLE
            && slots[i].tls == (tls_mem_t *)0
#endif
            ) {
            open++;
        }
    }
    if (http_port != 0u) {
        n = snprintf(buf + at, max - at, "http %u /mcp, %u of %u connected\n",
                     (unsigned)http_port, open, (unsigned)TIKU_MCP_HTTP_CONNS);
    } else {
        n = snprintf(buf + at, max - at, "http off\n");
    }
    if (n < 0 || (size_t)n >= max - at) {
        return n < 0 ? n : (int)(at + (size_t)n);
    }
    at += (size_t)n;
#if TIKU_MCP_TLS_ENABLE
    for (open = 0u, i = 1u; i < TIKU_MCP_TCP_SLOTS; i++) {
        if (slots[i].m != (conn_mem_t *)0 && slots[i].tls != (tls_mem_t *)0) {
            open++;
        }
    }
    if (https_port != 0u) {
        n = snprintf(buf + at, max - at, "https %u /mcp, %u of %u connected\n",
                     (unsigned)https_port, open, (unsigned)TIKU_MCP_HTTP_CONNS);
    } else {
        n = snprintf(buf + at, max - at, "https off%s\n",
                     tiku_mcp_tls_cred() ? "" : ", no certificate");
    }
    if (n < 0 || (size_t)n >= max - at) {
        return n < 0 ? n : (int)(at + (size_t)n);
    }
    at += (size_t)n;
#endif
    n = snprintf(buf + at, max - at, "accepted %lu refused %lu%s%s\n",
                 (unsigned long)accepted, (unsigned long)refused,
                 why[0] ? ", last: " : "", why);
    return n < 0 ? n : (int)(at + (size_t)n);
}

#else

int
tiku_mcp_tcp_listen(uint16_t p, int kind)
{
    (void)kind;
    return p == 0u ? 0 : -1;
}

tinymcp_t *
tiku_mcp_tcp_stream(unsigned i)
{
    (void)i;
    return (tinymcp_t *)0;
}

int
tiku_mcp_tcp_owns(const tinymcp_t *stream)
{
    (void)stream;
    return 0;
}

int
tiku_mcp_tcp_keyed(tinymcp_t *stream)
{
    (void)stream;
    (void)key_equals;
    return 0;
}

void
tiku_mcp_tcp_pump(void)
{
}

int
tiku_mcp_tcp_send(tinymcp_t *stream, const char *msg, size_t len)
{
    (void)stream;
    (void)msg;
    (void)len;
    return -1;
}

int
tiku_mcp_tcp_send_part(tinymcp_t *stream, const char *bytes, size_t len,
                       int last)
{
    (void)stream;
    (void)bytes;
    (void)len;
    (void)last;
    return -1;
}

int
tiku_mcp_tcp_read(char *buf, size_t max)
{
    return snprintf(buf, max, "off: this build has no TCP\n");
}

#endif /* TIKU_MCP_TCP_ENABLE */
