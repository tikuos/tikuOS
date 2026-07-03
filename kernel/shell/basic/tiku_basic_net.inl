/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_net.inl - networking words for the full BASIC profile
 * (RP2350 / Apollo): UDPSEND (instant), MQTTPUB (tick-driven via a pump
 * helper), HTTPGET$ (bounded blocking). The numeric/string accessors
 * (IPADDR$, NETUP, HTTPGET$, HTTPSTATUS) live in tiku_basic_string.inl /
 * tiku_basic_call.inl; the statements + the cooperative pump live here.
 *
 * Cooperative-blocking rule: exec_run only pumps the console between
 * statements, so any net op that waits MUST pump the console itself or it
 * hard-hangs the board (the ADC-hang class). The MQTT path pumps via
 * basic_net_mqtt_pump(); UDP is instant; HTTP self-pumps the net stack.
 *
 * NOT a standalone translation unit -- included from tiku_basic.c after
 * tiku_basic_stmt.inl (it reuses parse_strexpr / parse_path_literal).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_NET_ENABLE

/* Parse a dotted-quad "a.b.c.d" into 4 bytes. Returns 0 on success. */
static int
basic_net_parse_ip(const char *s, uint8_t out[4])
{
    int i, digits;
    long v;
    for (i = 0; i < 4; i++) {
        v = 0; digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            if (v > 255) return -1;
            s++; digits++;
        }
        if (digits == 0) return -1;
        out[i] = (uint8_t)v;
        if (i < 3) { if (*s != '.') return -1; s++; }
    }
    return (*s == '\0') ? 0 : -1;
}

#if (TIKU_KITS_NET_HTTP_ENABLE + 0)
/* HTTPHEADER "Name", value$ -- append a request header sent by the next
 * HTTPGET$/HTTPPOST$ (e.g. HTTPHEADER "Authorization", "Bearer " + K$).  Bare
 * HTTPHEADER (no args) clears them; headers otherwise accumulate. */
static void
exec_httpheader(const char **p)
{
    char   name[48], val[TIKU_BASIC_STR_BUF_CAP];
    size_t nl, vl, cur;
    skip_ws(p);
    if (**p == '\0' || **p == ':') {        /* bare HTTPHEADER -> clear */
        basic_http_hdrs[0] = '\0';
        return;
    }
    if (parse_strexpr(p, name, sizeof(name)) != 0) return;
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return;
    }
    (*p)++;
    if (parse_strexpr(p, val, sizeof(val)) != 0) return;
    nl = strlen(name); vl = strlen(val); cur = strlen(basic_http_hdrs);
    if (cur + nl + vl + 4u >= sizeof(basic_http_hdrs)) {
        basic_error = 1; SHELL_PRINTF(SH_RED "? too many headers\n" SH_RST); return;
    }
    memcpy(basic_http_hdrs + cur, name, nl); cur += nl;
    basic_http_hdrs[cur++] = ':'; basic_http_hdrs[cur++] = ' ';
    memcpy(basic_http_hdrs + cur, val, vl); cur += vl;
    basic_http_hdrs[cur++] = '\r'; basic_http_hdrs[cur++] = '\n';
    basic_http_hdrs[cur] = '\0';
}
#if TIKU_BASIC_BIGBUF_COUNT > 0
/* FETCH #n, "host", "path" [, body$] -- GET (or POST when body$ is given)
 * straight into big-buffer #n, past the STR_BUF_CAP limit, so a whole multi-KB
 * reply is retained. Read it with JSON$(#n,...), LINE$(#n,i), BETWEEN$(#n,a$,b$)
 * and LEN(#n); HTTPSTATUS() reports the code. Any HTTPHEADER lines apply. */
static void
exec_fetch(const char **p)
{
    long n;
    char host[64], path[80], body[TIKU_BASIC_STR_BUF_CAP];
    int  have_body = 0, rc;
    skip_ws(p);
    if (**p != '#') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? '#buffer' expected\n" SH_RST); return;
    }
    (*p)++;
    n = parse_expr(p);
    if (basic_error) return;
    if (n < 0 || n >= TIKU_BASIC_BIGBUF_COUNT || basic_bigbuf[n] == NULL) {
        basic_error = 1; SHELL_PRINTF(SH_RED "? bad #buffer\n" SH_RST); return;
    }
    skip_ws(p);
    if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return; }
    (*p)++;
    if (parse_path_literal(p, host, sizeof(host)) != 0) return;
    skip_ws(p);
    if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return; }
    (*p)++;
    if (parse_path_literal(p, path, sizeof(path)) != 0) return;
    skip_ws(p);
    if (**p == ',') {                       /* optional body -> POST */
        (*p)++;
        if (parse_strexpr(p, body, sizeof(body)) != 0) return;
        have_body = 1;
    }
    rc = basic_https_get(have_body ? "POST" : "GET", host, path,
                         have_body ? body : NULL, NULL,
                         basic_bigbuf[n], (size_t)TIKU_BASIC_BIGBUF_SIZE);
    /* basic_https_get stores the whole reply (status line + headers + body).
     * The #n extractors (JSON$/LINE$/BETWEEN$) want the reply BODY -- a JSON$
     * parse from byte 0 would choke on "HTTP/1.1 ..." -- so drop the header
     * block here: keep everything past the first blank line (CRLF CRLF).
     * HTTPSTATUS() still reports the code.  A reply with no header terminator
     * (odd or truncated) is kept whole rather than discarded. */
    if (rc > 0) {
        char  *buf = basic_bigbuf[n];
        size_t total = (size_t)rc, i, hdr = 0;
        for (i = 0; i + 3u < total; i++) {
            if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                buf[i + 2] == '\r' && buf[i + 3] == '\n') { hdr = i + 4u; break; }
        }
        if (hdr > 0) {                  /* shift the body down over the headers */
            size_t blen = total - hdr, j;
            for (j = 0; j < blen; j++) buf[j] = buf[hdr + j];
            buf[blen] = '\0';
            basic_biglen[n] = blen;
        } else {
            basic_biglen[n] = total;
        }
    } else {
        basic_biglen[n] = 0;
    }
}
#endif
#endif

/* UDPSEND "a.b.c.d", port, expr$ -- fire-and-forget a datagram. Instant
 * (the stack queues + transmits synchronously), so no pump needed. */
static void
exec_udpsend(const char **p)
{
    char    ipstr[20];
    char    payload[TIKU_BASIC_STR_BUF_CAP];
    uint8_t ip[4];
    long    port;

    if (parse_path_literal(p, ipstr, sizeof(ipstr)) != 0) return;
    if (basic_net_parse_ip(ipstr, ip) != 0) {
        basic_error = 1; SHELL_PRINTF(SH_RED "? bad IP '%s'\n" SH_RST, ipstr); return;
    }
    skip_ws(p);
    if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return; }
    (*p)++;
    port = parse_expr(p);
    if (basic_error) return;
    if (port < 1 || port > 65535) {
        basic_error = 1; SHELL_PRINTF(SH_RED "? UDP port out of range\n" SH_RST); return;
    }
    skip_ws(p);
    if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return; }
    (*p)++;
    if (parse_strexpr(p, payload, sizeof(payload)) != 0) return;
    if (tiku_kits_net_udp_send(ip, (uint16_t)port, 5000U,
                               (const uint8_t *)payload,
                               (uint16_t)strlen(payload)) != TIKU_KITS_NET_OK) {
        basic_error = 1;
        basic_errcat = TIKU_BASIC_ERR_NET;
        SHELL_PRINTF(SH_RED "? UDP send failed (is the IP link up? 'wifi up')\n"
                     SH_RST);
    }
}

#if (TIKU_KITS_NET_HTTP_ENABLE + 0)
/* BROWSE "host[/path]" -- fetch a page over cert-TLS and render it to the
 * console as plain text (the BASIC web browser). Unlike STRIP$(HTTPGET$(...)),
 * which is bounded by the BASIC string scratch, this fetches into a dedicated
 * buffer so it shows a whole simple page. basic_https_get self-pumps the net
 * stack, so no separate pump loop is needed here. */
#ifndef TIKU_BASIC_BROWSE_BUF
#define TIKU_BASIC_BROWSE_BUF  16384
#endif
static char basic_browse_buf[TIKU_BASIC_BROWSE_BUF];
/* If resp is an HTTP 3xx redirect carrying a Location header, copy the target
 * URL into out[outcap] and return 1; otherwise return 0.  Only the header
 * region (before the blank line) is scanned. */
static int
basic_http_redirect(const char *resp, char *out, size_t outcap)
{
    const char *body = strstr(resp, "\r\n\r\n");
    const char *sp   = strchr(resp, ' ');
    const char *line, *d;
    int code;

    if (sp == (const char *)0) return 0;
    for (d = sp + 1; *d == ' '; d++) { }
    if (d[0] < '0' || d[0] > '9' || d[1] < '0' || d[1] > '9' ||
        d[2] < '0' || d[2] > '9') return 0;
    code = (d[0] - '0') * 100 + (d[1] - '0') * 10 + (d[2] - '0');
    if (code < 300 || code >= 400) return 0;

    for (line = resp; line && (!body || line <= body); ) {
        if (basic_ci_starts(line, "location:")) {
            const char *h = line + 9, *e;
            size_t n;
            while (*h == ' ' || *h == '\t') h++;
            for (e = h; *e && *e != '\r' && *e != '\n'; e++) { }
            n = (size_t)(e - h);
            if (n == 0 || n >= outcap) return 0;
            memcpy(out, h, n);
            out[n] = '\0';
            return 1;
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    return 0;
}

/* BROWSE "host[/path]" -- fetch a page over cert-TLS and render it to the
 * console as plain text (the BASIC web browser).  Follows up to 3 HTTP
 * redirects (so e.g. google.com -> www.google.com lands on the real page),
 * handling absolute and same-host relative Location targets. basic_https_get
 * self-pumps the net stack, so no separate pump loop is needed here. */
static void
exec_browse(const char **p)
{
    char        url[200];
    char        host[100];
    const char *u, *path;
    int         i, hop;

    if (parse_strexpr(p, url, sizeof url) != 0) return;
    host[0] = '\0';
    for (hop = 0; hop < 4; hop++) {
        u = url;
        if      (basic_ci_starts(u, "https://")) u += 8;
        else if (basic_ci_starts(u, "http://"))  u += 7;
        if (u[0] == '/') {
            path = u;                       /* relative redirect: keep host */
        } else {
            for (i = 0; u[i] && u[i] != '/' && i < (int)sizeof host - 1; i++) {
                host[i] = u[i];
            }
            host[i] = '\0';
            path = (u[i] == '/') ? (u + i) : "/";
        }
        if (host[0] == '\0') {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? BROWSE: empty URL\n" SH_RST);
            return;
        }
        if (basic_https_get("GET", host, path, NULL, NULL, basic_browse_buf,
                            sizeof basic_browse_buf) < 0) {
            basic_error = 1;      /* basic_https_get already printed the reason */
            basic_errcat = TIKU_BASIC_ERR_NET;
            return;
        }
        if (hop < 3 && basic_http_redirect(basic_browse_buf, url, sizeof url)) {
            SHELL_PRINTF("  -> %s\n", url);
            continue;
        }
        break;
    }
    /* Compact status line: HTTP code + body size, so an empty or all-markup
     * page (which renders to nothing) is explained rather than just blank. */
    {
        const char *sp = strchr(basic_browse_buf, ' ');
        const char *bd = strstr(basic_browse_buf, "\r\n\r\n");
        int code = (sp && sp[1] >= '0' && sp[1] <= '9')
                 ? (sp[1] - '0') * 100 + (sp[2] - '0') * 10 + (sp[3] - '0') : 0;
        if (code == 0) {
            /* Nothing parsed -- surface WHY the TLS read broke.  The red stage
             * print is lost in the shared console/SLIP mux, so report the
             * post-handshake read break here instead: rdfail 1 no-record /
             * 2 wire-type / 3 decrypt-fail / 4 alert, with the wire record type
             * and the server app read-seq (which post-handshake record broke). */
            SHELL_PRINTF("[%s: HTTP 0, 0 B  rdfail=%d type=%u seq=%u]\n", host,
                         tiku_kits_crypto_tls13_last_read_fail,
                         (unsigned)tiku_kits_crypto_tls13_last_read_type,
                         (unsigned)tiku_kits_crypto_tls13_last_read_seq);
        } else {
            SHELL_PRINTF("[%s: HTTP %d, %u B]\n", host, code,
                         (unsigned)(bd ? strlen(bd + 4) : 0));
        }
    }
    basic_html_render(basic_browse_buf, (char *)0, 0);   /* NULL out = print */
}
#endif /* TIKU_KITS_NET_HTTP_ENABLE */

#if (TIKU_KITS_NET_MQTT_ENABLE + 0)
/* MQTT publish (QoS 0). The broker exchange is poll-based, so we drive it
 * across a bounded deadline, pumping the console between polls so the board
 * never hard-hangs (the ADC-hang class). */
static volatile uint8_t basic_mqtt_evt;
static void basic_mqtt_event_cb(uint8_t e) { basic_mqtt_evt = e; }

/* Inbound capture for MQTTWAIT$: the last PUBLISH the broker delivered,
 * copied out of the transient callback buffers (which are only valid for
 * the callback's duration) into bounded static storage.  `rx_pending`
 * latches until the waiter consumes it. */
static volatile uint8_t basic_mqtt_rx_pending;
static char basic_mqtt_rx_topic[48];
static char basic_mqtt_rx_msg[TIKU_BASIC_MQTT_RX_CAP];
static void basic_mqtt_msg_cb(const char *t, uint16_t tl, const uint8_t *d,
                              uint16_t dl, uint8_t q, uint8_t r)
{
    uint16_t n;
    (void)q; (void)r;
    n = (tl < sizeof(basic_mqtt_rx_topic) - 1u)
        ? tl : (uint16_t)(sizeof(basic_mqtt_rx_topic) - 1u);
    memcpy(basic_mqtt_rx_topic, t, n);
    basic_mqtt_rx_topic[n] = '\0';
    n = (dl < sizeof(basic_mqtt_rx_msg) - 1u)
        ? dl : (uint16_t)(sizeof(basic_mqtt_rx_msg) - 1u);
    memcpy(basic_mqtt_rx_msg, d, n);
    basic_mqtt_rx_msg[n] = '\0';
    basic_mqtt_rx_pending = 1;
}

/* One pump step: drive MQTT keepalive/state (paced ~8 Hz), service the console
 * transport (tiku_shell_io_rx_ready() also services the USB-CDC poll), kick the
 * watchdog. Returns 1 on Ctrl-C. */
static int
basic_net_mqtt_pump(void)
{
    static tiku_clock_time_t last;
    tiku_clock_time_t now = tiku_clock_time();
    tiku_watchdog_kick();
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
    /* Drive the WiFi RX drain ourselves, every iteration: the
     * cyw43_runner process is starved while we busy-wait here, so
     * without this the chip's F2 FIFO fills and the SYN-ACK / CONNACK
     * never reach the TCP/MQTT stack -- connect would always time out. */
    (void)whd_drain_rx();
#endif
    if ((tiku_clock_time_t)(now - last) >=
        (tiku_clock_time_t)(TIKU_CLOCK_SECOND / 8)) {
        last = now;
        /* TCP first (advances the connect handshake / retransmits / ACKs),
         * then MQTT reacts to the resulting connection events. The shell's
         * async tick normally drives tcp_periodic(); we own the loop here. */
        tiku_kits_net_tcp_periodic();
        tiku_kits_net_mqtt_periodic();
    }
    /* Ctrl-C break.  On a SLIP build the console and the IP link share one
     * UART, so read through the SLIP-aware demux (as exec_delay_ms does):
     * it routes IP frames to the stack and returns only genuine console
     * bytes.  The raw getc would misread a payload byte 0x03 as Ctrl-C --
     * aborting a connect early with an uncategorised error -- and would
     * also steal bytes meant for the TCP/MQTT stack. */
#if TIKU_SHELL_CMD_SLIP
    if (tiku_shell_net_getc() == BASIC_CTRL_C) return 1;
#else
    if (tiku_shell_io_rx_ready()) {
        if (tiku_shell_io_getc() == BASIC_CTRL_C) return 1;
    }
#endif
    return 0;
}

/* MQTTPUB "broker_ip", "topic", expr$ -- connect, publish QoS0, disconnect. */
static void
exec_mqttpub(const char **p)
{
    char    ipstr[20];
    char    topic[48];
    char    payload[TIKU_BASIC_STR_BUF_CAP];
    uint8_t ip[4];
    tiku_clock_time_t deadline;

    if (parse_path_literal(p, ipstr, sizeof(ipstr)) != 0) return;
    if (basic_net_parse_ip(ipstr, ip) != 0) {
        basic_error = 1; SHELL_PRINTF(SH_RED "? bad broker IP '%s'\n" SH_RST, ipstr); return;
    }
    skip_ws(p);
    if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return; }
    (*p)++;
    if (parse_path_literal(p, topic, sizeof(topic)) != 0) return;
    skip_ws(p);
    if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return; }
    (*p)++;
    if (parse_strexpr(p, payload, sizeof(payload)) != 0) return;

    /* Initialise the TCP connection table. On a lean WiFi profile nothing
     * else does: the NET_TEST init and the SLIP net process (the two normal
     * tcp_init sites) are both absent here, so without this mqtt_connect()'s
     * tcp_connect() allocates from an uninitialised table and never
     * establishes. Idempotent for BASIC -- each MQTTPUB is a fresh
     * connect/publish/disconnect with no persistent connections. */
    tiku_kits_net_tcp_init();
    tiku_kits_net_mqtt_init();
    tiku_kits_net_mqtt_set_server(ip, 1883);
    tiku_kits_net_mqtt_set_credentials("tikubasic", (const char *)0,
                                       (const char *)0);
    basic_mqtt_evt = 0xFFu;
    if (tiku_kits_net_mqtt_connect(basic_mqtt_msg_cb, basic_mqtt_event_cb)
        != TIKU_KITS_NET_OK) {
        basic_error = 1;
        basic_errcat = TIKU_BASIC_ERR_NET;
        SHELL_PRINTF(SH_RED "? MQTT connect rejected (IP link up? 'wifi up')\n" SH_RST);
        return;
    }
    deadline = (tiku_clock_time_t)(tiku_clock_time() + 8u * TIKU_CLOCK_SECOND);
    while (!tiku_kits_net_mqtt_is_connected() &&
           TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
        if (basic_net_mqtt_pump()) {
            tiku_kits_net_mqtt_disconnect();
            basic_error = 1; SHELL_PRINTF(SH_YELLOW "^C\n" SH_RST); return;
        }
    }
    if (!tiku_kits_net_mqtt_is_connected()) {
        tiku_kits_net_mqtt_disconnect();
        basic_error = 1; basic_errcat = TIKU_BASIC_ERR_NET;
        SHELL_PRINTF(SH_RED "? MQTT connect timeout\n" SH_RST); return;
    }
    tiku_kits_net_mqtt_publish(topic, (const uint8_t *)payload,
                               (uint16_t)strlen(payload), 0, 0);
    /* let the publish flush, then close cleanly */
    deadline = (tiku_clock_time_t)(tiku_clock_time() + 2u * TIKU_CLOCK_SECOND);
    while (TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
        if (basic_net_mqtt_pump()) break;
    }
    tiku_kits_net_mqtt_disconnect();
    deadline = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND / 2);
    while (TIKU_CLOCK_LT(tiku_clock_time(), deadline)) (void)basic_net_mqtt_pump();
}

/* MQTTWAIT$("broker_ip", "topic", secs) helper -- the inbound dual of
 * MQTTPUB.  Connect, SUBSCRIBE to `topic`, pump until one PUBLISH lands
 * or `secs` elapses, then disconnect; the payload is written to out[cap]
 * ("" on timeout).  Returns 0 if a message arrived, -1 on timeout, and
 * sets basic_error (category NET) on a hard failure (bad IP / connect).
 * Reuses the exact connect/pump/disconnect lifecycle MQTTPUB is proven
 * on -- no persistent connection is held across statements. */
static int
basic_net_mqtt_wait(const char *ipstr, const char *topic, long secs,
                    char *out, size_t cap)
{
    uint8_t ip[4];
    tiku_clock_time_t deadline;

    if (cap) out[0] = '\0';
    if (basic_net_parse_ip(ipstr, ip) != 0) {
        basic_error = 1; basic_errcat = TIKU_BASIC_ERR_NET;
        SHELL_PRINTF(SH_RED "? bad broker IP '%s'\n" SH_RST, ipstr);
        return -1;
    }
    if (secs <= 0)    secs = 1;
    if (secs > 3600L) secs = 3600L;

    tiku_kits_net_tcp_init();
    tiku_kits_net_mqtt_init();
    tiku_kits_net_mqtt_set_server(ip, 1883);
    tiku_kits_net_mqtt_set_credentials("tikubasic", (const char *)0,
                                       (const char *)0);
    basic_mqtt_evt        = 0xFFu;
    basic_mqtt_rx_pending = 0;
    if (tiku_kits_net_mqtt_connect(basic_mqtt_msg_cb, basic_mqtt_event_cb)
        != TIKU_KITS_NET_OK) {
        basic_error = 1; basic_errcat = TIKU_BASIC_ERR_NET;
        SHELL_PRINTF(SH_RED "? MQTT connect rejected (IP link up? 'wifi up')\n" SH_RST);
        return -1;
    }
    deadline = (tiku_clock_time_t)(tiku_clock_time() + 8u * TIKU_CLOCK_SECOND);
    while (!tiku_kits_net_mqtt_is_connected() &&
           TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
        if (basic_net_mqtt_pump()) {
            tiku_kits_net_mqtt_disconnect();
            basic_error = 1; SHELL_PRINTF(SH_YELLOW "^C\n" SH_RST); return -1;
        }
    }
    if (!tiku_kits_net_mqtt_is_connected()) {
        tiku_kits_net_mqtt_disconnect();
        basic_error = 1; basic_errcat = TIKU_BASIC_ERR_NET;
        SHELL_PRINTF(SH_RED "? MQTT connect timeout\n" SH_RST); return -1;
    }
    tiku_kits_net_mqtt_subscribe(topic, 0);
    /* Pump until a PUBLISH lands (msg_cb latches rx_pending) or the
     * caller's timeout expires.  Ctrl-C aborts early. */
    deadline = (tiku_clock_time_t)(tiku_clock_time()
                   + (tiku_clock_time_t)((tiku_clock_time_t)secs
                                         * TIKU_CLOCK_SECOND));
    while (!basic_mqtt_rx_pending &&
           TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
        if (basic_net_mqtt_pump()) break;
    }
    tiku_kits_net_mqtt_disconnect();
    {   /* flush the DISCONNECT before returning */
        tiku_clock_time_t d2 =
            (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND / 2);
        while (TIKU_CLOCK_LT(tiku_clock_time(), d2))
            (void)basic_net_mqtt_pump();
    }
    if (basic_mqtt_rx_pending) {
        size_t n = strlen(basic_mqtt_rx_msg);
        if (cap == 0) return 0;
        if (n + 1u > cap) n = cap - 1u;
        memcpy(out, basic_mqtt_rx_msg, n);
        out[n] = '\0';
        basic_mqtt_rx_pending = 0;
        return 0;
    }
    return -1;   /* timeout: out already "" */
}
#endif /* TIKU_KITS_NET_MQTT_ENABLE */

#endif /* TIKU_BASIC_NET_ENABLE */
