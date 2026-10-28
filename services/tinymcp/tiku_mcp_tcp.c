/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_tcp.c - one TCP connection carrying newline-delimited MCP, its
 * buffers leased from the SRAM tier while it is open, and /sys/mcp/key.
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
#include <kernel/vfs/tiku_vfs.h>

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

int
tiku_mcp_tcp_keyed(tinymcp_t *stream)
{
    char sent[TIKU_MCP_KEY_MAX + 1];
    uint8_t diff = 0u;
    int n;
    size_t i;

    if (stream == (tinymcp_t *)0 || !key_set()) {
        return 0;
    }
    n = tinymcp_meta(stream, TIKU_MCP_KEY_META, sent, sizeof sent);
    if (n < 0 || (size_t)n != strlen(key_persist)) {
        return 0;
    }
    /* Every byte is compared, whatever the first difference. */
    for (i = 0u; i < (size_t)n; i++) {
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

/*---------------------------------------------------------------------------*/
/* THE CONNECTION                                                            */
/*---------------------------------------------------------------------------*/

#define ARENA_ID 0x4Du             /* 'M' */

static tinymcp_t                 stream;
static tiku_kits_net_tcp_conn_t *conn;      /* the TCB, NULL once it went */
static uint8_t                   open;      /* stream and arena are live */
static tiku_arena_t              arena;     /* its in and out buffers */
static uint16_t                  port;      /* listening on, 0: not */
static uint32_t                  accepted, refused;
static const char               *why = "";  /* the last refusal's reason */

/* A reply's bytes wait here until a segment's worth is ready. */
static uint8_t  stage[TIKU_KITS_NET_TCP_MSS];
static uint16_t staged;

/**
 * @brief Release a connection that went: its subscriptions, kept calls and
 *        buffers.  Only from the pump, where no request is being served, so
 *        the library never writes into a buffer already given back.
 */
static void
release(void)
{
    if (!open || conn != (tiku_kits_net_tcp_conn_t *)0) {
        return;
    }
    open = 0u;
    staged = 0u;
    tinymcp_closed(&stream);
    /* The buffers held requests that carried the key: zeroed, then given
     * back. */
    (void)tiku_arena_secure_reset(&arena);
    (void)tiku_arena_destroy(&arena);
}

static void
tcp_recv(tiku_kits_net_tcp_conn_t *c, uint16_t available)
{
    (void)c;
    (void)available;            /* the pump reads it */
}

static void
tcp_event(tiku_kits_net_tcp_conn_t *c, uint8_t event)
{
    char *in, *out;

    if (event != TIKU_KITS_NET_TCP_EVT_CONNECTED) {
        if (c == conn) {
            conn = (tiku_kits_net_tcp_conn_t *)0;   /* the pump releases it */
        }
        return;
    }
    if (open) {
        tiku_kits_net_tcp_abort(c);         /* one connection at a time */
        refused++;
        why = "busy";
        return;
    }
    if (tiku_tier_arena_create(&arena, TIKU_MEM_SRAM,
                               TIKU_MCP_IN_CAP + TIKU_MCP_OUT_CAP + 8u,
                               ARENA_ID) != TIKU_MEM_OK) {
        tiku_kits_net_tcp_abort(c);
        refused++;
        why = "no SRAM";
        return;
    }
    in = tiku_arena_alloc(&arena, TIKU_MCP_IN_CAP);
    out = tiku_arena_alloc(&arena, TIKU_MCP_OUT_CAP);
    if (in == (char *)0 || out == (char *)0) {
        (void)tiku_arena_reset(&arena);
        (void)tiku_arena_destroy(&arena);
        tiku_kits_net_tcp_abort(c);
        refused++;
        why = "no SRAM";
        return;
    }
    tinymcp_init(&stream, &tiku_mcp_host, &stream, in, TIKU_MCP_IN_CAP, out,
                 TIKU_MCP_OUT_CAP, TIKU_MCP_SUBS);
    conn = c;
    open = 1u;
    staged = 0u;
    accepted++;
}

int
tiku_mcp_tcp_listen(uint16_t p)
{
    if (port != 0u) {
        (void)tiku_kits_net_tcp_unlisten(port);
        port = 0u;
    }
    if (p == 0u) {
        return 0;
    }
    tiku_kits_net_tcp_init();
    if (tiku_kits_net_tcp_listen(p, tcp_recv, tcp_event) !=
        TIKU_KITS_NET_OK) {
        return -1;
    }
    port = p;
    return 0;
}

tinymcp_t *
tiku_mcp_tcp_stream(void)
{
    return open ? &stream : (tinymcp_t *)0;
}

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

void
tiku_mcp_tcp_pump(void)
{
    uint8_t chunk[128];
    uint16_t n;

    if (port != 0u || open) {
        timers();
    }

    if (conn != (tiku_kits_net_tcp_conn_t *)0 &&
        conn->state == TIKU_KITS_NET_TCP_STATE_CLOSE_WAIT) {
        (void)tiku_kits_net_tcp_close(conn);
        conn = (tiku_kits_net_tcp_conn_t *)0;
    }
    while (conn != (tiku_kits_net_tcp_conn_t *)0 &&
           (n = tiku_kits_net_tcp_read(conn, chunk, sizeof chunk)) > 0u) {
        tiku_mcp_identity();
        (void)tinymcp_feed(&stream, chunk, n);
    }
    release();
}

/*---------------------------------------------------------------------------*/
/* SENDING                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief The staged bytes as one segment.  While the TX pool is full the
 *        console is pumped, so the peer's acknowledgements come in.
 * @return 0, or -1 when the connection went or no segment came free within
 *         TIKU_MCP_TCP_SEND_MS, in which case it is aborted
 */
static int
flush_stage(void)
{
    tiku_clock_time_t start = tiku_clock_time();

    while (staged > 0u) {
        if (conn == (tiku_kits_net_tcp_conn_t *)0) {
            return -1;
        }
        if (tiku_kits_net_tcp_send(conn, stage, staged) == TIKU_KITS_NET_OK) {
            staged = 0u;
            return 0;
        }
        if ((tiku_clock_time_t)(tiku_clock_time() - start) >=
            TIKU_CLOCK_MS_TO_TICKS(TIKU_MCP_TCP_SEND_MS)) {
            tiku_kits_net_tcp_abort(conn);
            conn = (tiku_kits_net_tcp_conn_t *)0;   /* the pump releases it */
            return -1;
        }
        tiku_watchdog_kick();
        tiku_console_pump();
    }
    return 0;
}

/** @brief @p len bytes into the stage, a segment sent each time it fills. */
static int
put(const char *p, size_t len)
{
    while (len > 0u) {
        uint16_t room, take;

        if (conn == (tiku_kits_net_tcp_conn_t *)0) {
            return -1;
        }
        room = conn->snd_mss < sizeof stage ? conn->snd_mss
                                           : (uint16_t)sizeof stage;
        take = (uint16_t)(len < (size_t)(room - staged) ? len
                                                        : (size_t)(room - staged));
        memcpy(stage + staged, p, take);
        staged = (uint16_t)(staged + take);
        p += take;
        len -= take;
        if (staged == room && flush_stage() != 0) {
            return -1;
        }
    }
    return 0;
}

int
tiku_mcp_tcp_send_part(const char *bytes, size_t len, int last)
{
    if (put(bytes, len) != 0) {
        return -1;
    }
    if (!last) {
        return 0;
    }
    return put("\n", 1u) == 0 ? flush_stage() : -1;
}

int
tiku_mcp_tcp_send(const char *msg, size_t len)
{
    return tiku_mcp_tcp_send_part(msg, len, 1);
}

int
tiku_mcp_tcp_read(char *buf, size_t max)
{
    int n;

    if (conn != (tiku_kits_net_tcp_conn_t *)0) {
        const uint8_t *a = conn->remote_addr.b;

        n = snprintf(buf, max, "connected %u %u.%u.%u.%u:%u\n",
                     (unsigned)port, a[0], a[1], a[2], a[3],
                     (unsigned)conn->remote_port);
    } else if (port != 0u) {
        n = snprintf(buf, max, "listening %u\n", (unsigned)port);
    } else {
        n = snprintf(buf, max, "off\n");
    }
    if (n < 0 || (size_t)n >= max) {
        return n;
    }
    return n + snprintf(buf + n, max - (size_t)n,
                        "accepted %lu refused %lu%s%s\n",
                        (unsigned long)accepted, (unsigned long)refused,
                        why[0] ? ", last: " : "", why);
}

uint32_t
tiku_mcp_tcp_accepted(void)
{
    return accepted;
}

uint32_t
tiku_mcp_tcp_refused(void)
{
    return refused;
}

#else

int
tiku_mcp_tcp_listen(uint16_t p)
{
    return p == 0u ? 0 : -1;
}

tinymcp_t *
tiku_mcp_tcp_stream(void)
{
    return (tinymcp_t *)0;
}

void
tiku_mcp_tcp_pump(void)
{
}

int
tiku_mcp_tcp_send(const char *msg, size_t len)
{
    (void)msg;
    (void)len;
    return -1;
}

int
tiku_mcp_tcp_send_part(const char *bytes, size_t len, int last)
{
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

uint32_t
tiku_mcp_tcp_accepted(void)
{
    return 0u;
}

uint32_t
tiku_mcp_tcp_refused(void)
{
    return 0u;
}

#endif /* TIKU_MCP_TCP_ENABLE */
