/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_net.inl - networking statements for the full BASIC profile.
 *
 * A net word that waits pumps the console and the stack itself: the RUN loop
 * services them only between statements.  UDPSEND returns at once; MQTT pumps
 * through basic_net_mqtt_pump() and HTTPS through basic_https_pump().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_NET_ENABLE

/**
 * @brief Parse a dotted quad "a.b.c.d" into @p out.
 * @return 0 on success, -1 if @p s is not a dotted quad.
 */
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
/**
 * @brief HTTPHEADER "Name", value$: add a header to the following requests.
 *
 * E.g. HTTPHEADER "Authorization", "Bearer " + K$.  Headers accumulate and
 * go with every HTTPS request; a bare HTTPHEADER clears them.
 */
static void
exec_httpheader(const char **p)
{
    char   name[48], val[TIKU_BASIC_STR_BUF_CAP];
    size_t nl, vl, cur;
    skip_ws(p);
    if (cur_peek(p) == '\0' || cur_peek(p) == ':') {        /* bare: clear */
        basic_http_hdrs[0] = '\0';
        return;
    }
    if (parse_strexpr(p, name, sizeof(name)) != 0) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return;
    }
    cur_advance(p);
    if (parse_strexpr(p, val, sizeof(val)) != 0) return;
    nl = strlen(name); vl = strlen(val); cur = strlen(basic_http_hdrs);
    if (cur + nl + vl + 4u >= sizeof(basic_http_hdrs)) {
        basic_throw(TIKU_BASIC_ERR_NOMEM, "too many headers"); return;
    }
    memcpy(basic_http_hdrs + cur, name, nl); cur += nl;
    basic_http_hdrs[cur++] = ':'; basic_http_hdrs[cur++] = ' ';
    memcpy(basic_http_hdrs + cur, val, vl); cur += vl;
    basic_http_hdrs[cur++] = '\r'; basic_http_hdrs[cur++] = '\n';
    basic_http_hdrs[cur] = '\0';
}
#if TIKU_BASIC_BIGBUF_COUNT > 0
/**
 * @brief FETCH #n, "host", "path" [, body$]: GET, or POST with body$, into #n.
 *
 * The reply body lands in big buffer #n, so a reply past STR_BUF_CAP is kept,
 * up to TIKU_BASIC_BIGBUF_SIZE; read it with JSON$(#n,...), LINE$(#n,i),
 * BETWEEN$(#n,a$,b$) and LEN(#n).  HTTPHEADER lines apply.
 */
static void
exec_fetch(const char **p)
{
    long n;
    char host[TIKU_BASIC_HTTP_HOST_MAX], path[TIKU_BASIC_HTTP_PATH_MAX];
    char body[TIKU_BASIC_STR_BUF_CAP];
    int  have_body = 0, rc;
    skip_ws(p);
    if (cur_peek(p) != '#') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'#buffer' expected"); return;
    }
    cur_advance(p);
    n = parse_expr(p);
    if (basic_error) return;
    if (n < 0 || n >= TIKU_BASIC_BIGBUF_COUNT || basic_bigbuf[n] == NULL) {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "bad #buffer"); return;
    }
    skip_ws(p);
    if (cur_peek(p) != ',') { basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return; }
    cur_advance(p);
    if (parse_path_literal(p, host, sizeof(host)) != 0) return;
    skip_ws(p);
    if (cur_peek(p) != ',') { basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return; }
    cur_advance(p);
    if (parse_path_literal(p, path, sizeof(path)) != 0) return;
    skip_ws(p);
    if (cur_peek(p) == ',') {                       /* optional body -> POST */
        cur_advance(p);
        if (parse_strexpr(p, body, sizeof(body)) != 0) return;
        have_body = 1;
    }
    rc = basic_https_get(have_body ? "POST" : "GET", host, path,
                         have_body ? body : NULL, NULL,
                         basic_bigbuf[n], (size_t)TIKU_BASIC_BIGBUF_SIZE);
    /* basic_https_get() stores the whole reply (status line, headers, body);
     * the #n extractors (JSON$, LINE$, BETWEEN$) parse from byte 0, so the
     * header block is dropped here, keeping everything past the first blank
     * line (CRLF CRLF).  HTTPSTATUS() still reports the code.  A reply with no
     * header terminator is kept whole. */
    if (rc > 0) {
        char  *buf = basic_bigbuf[n];
        size_t total = (size_t)rc, i, hdr = 0;
        for (i = 0; i + 3u < total; i++) {
            if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                buf[i + 2] == '\r' && buf[i + 3] == '\n') { hdr = i + 4u; break; }
        }
        if (hdr > 0) {        /* shift the body down over the headers */
            size_t blen = total - hdr, j;
            for (j = 0; j < blen; j++) buf[j] = buf[hdr + j];
            buf[blen] = '\0';
            basic_biglen[n] = blen;
        } else {
            basic_biglen[n] = total;
        }
    } else {
        /* Terminate as well as zero the length: PRINT reads the buffer as a C
         * string. */
        basic_bigbuf[n][0] = '\0';
        basic_biglen[n] = 0;
    }
}
#endif
#endif

/**
 * @brief UDPSEND "a.b.c.d", port, expr$: send one datagram, fire and forget.
 *
 * The stack queues and transmits synchronously, so no pump is needed.
 */
static void
exec_udpsend(const char **p)
{
    char    ipstr[20];
    char    payload[TIKU_BASIC_STR_BUF_CAP];
    uint8_t ip[4];
    long    port;

    if (parse_path_literal(p, ipstr, sizeof(ipstr)) != 0) return;
    if (basic_net_parse_ip(ipstr, ip) != 0) {
        basic_throwf(TIKU_BASIC_ERR_SYNTAX, "bad IP '%s'", ipstr); return;
    }
    skip_ws(p);
    if (cur_peek(p) != ',') { basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return; }
    cur_advance(p);
    port = parse_expr(p);
    if (basic_error) return;
    if (port < 1 || port > 65535) {
        basic_throw(TIKU_BASIC_ERR_NET, "UDP port out of range"); return;
    }
    skip_ws(p);
    if (cur_peek(p) != ',') { basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return; }
    cur_advance(p);
    if (parse_strexpr(p, payload, sizeof(payload)) != 0) return;
    if (tiku_kits_net_udp_send(ip, (uint16_t)port, 5000U,
                               (const uint8_t *)payload,
                               (uint16_t)strlen(payload)) != TIKU_KITS_NET_OK) {
        basic_throw(TIKU_BASIC_ERR_NET, "UDP send failed (is the IP link up? 'wifi up')");
    }
}

#if (TIKU_KITS_NET_HTTP_ENABLE + 0)
/* BROWSE page buffer: one reply of up to TIKU_BASIC_BROWSE_BUF bytes, status
 * line and headers included. */
#ifndef TIKU_BASIC_BROWSE_BUF
#define TIKU_BASIC_BROWSE_BUF  16384
#endif
static char basic_browse_buf[TIKU_BASIC_BROWSE_BUF];
/**
 * @brief Copy the Location of an HTTP 3xx response @p resp into @p out.
 *
 * Only the header region, before the blank line, is scanned.
 *
 * @return 1 when @p resp is a redirect with a Location that fits, else 0.
 */
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

/* BROWSE splits its URL into the host and path it passes to basic_https_get()
 * (a relative redirect keeps the previous host), so these bound the request
 * BROWSE sends, as HOST_MAX and PATH_MAX do for the other callers. */
#ifndef TIKU_BASIC_BROWSE_URL_MAX
#define TIKU_BASIC_BROWSE_URL_MAX   200   /**< URL, incl. NUL */
#endif
#ifndef TIKU_BASIC_BROWSE_HOST_MAX
#define TIKU_BASIC_BROWSE_HOST_MAX  100   /**< host name, incl. NUL */
#endif
/* The worst BROWSE request: its path and host, the HTTPHEADER block and 64
 * bytes for the method, the fixed header names and the CRLFs. */
_Static_assert(TIKU_BASIC_BROWSE_URL_MAX + TIKU_BASIC_BROWSE_HOST_MAX +
               TIKU_BASIC_HTTP_HDRS_MAX + 64u <= TIKU_BASIC_HTTP_REQ_MAX,
               "BROWSE's URL does not fit the HTTP request buffer");

/**
 * @brief BROWSE "host[/path]": fetch a page over HTTPS, print it as text.
 *
 * Follows up to 3 redirects (e.g. google.com -> www.google.com), absolute or
 * same-host relative.  basic_https_get() pumps the net stack itself, so no
 * separate pump loop is needed.
 */
static void
exec_browse(const char **p)
{
    char        url[TIKU_BASIC_BROWSE_URL_MAX];
    char        host[TIKU_BASIC_BROWSE_HOST_MAX];
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
            basic_throw(TIKU_BASIC_ERR_GENERAL, "BROWSE: empty URL");
            return;
        }
        if (basic_https_get("GET", host, path, NULL, NULL, basic_browse_buf,
                            sizeof basic_browse_buf) < 0) {
            basic_error = 1;      /* basic_https_get printed the reason */
            basic_errcat = TIKU_BASIC_ERR_NET;
            return;
        }
        if (hop < 3 && basic_http_redirect(basic_browse_buf, url, sizeof url)) {
            SHELL_PRINTF("  -> %s\n", url);
            continue;
        }
        break;
    }
    /* Status line: the HTTP code and the body size, printed before the
     * rendered page, which is blank for an empty or all-markup page. */
    {
        const char *sp = strchr(basic_browse_buf, ' ');
        const char *bd = strstr(basic_browse_buf, "\r\n\r\n");
        int code = (sp && sp[1] >= '0' && sp[1] <= '9')
                 ? (sp[1] - '0') * 100 + (sp[2] - '0') * 10 + (sp[3] - '0') : 0;
        if (code == 0) {
            /* Nothing parsed: report how the post-handshake TLS read broke --
             * rdfail 1 no-record / 2 wire-type / 3 decrypt-fail / 4 alert,
             * with the wire record type and the server's application read
             * sequence number (which record broke). */
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
/* MQTT state for MQTTPUB (QoS 0) and MQTTWAIT$.  The broker exchange is
 * poll-based: every wait has a deadline and pumps the console and the net
 * stack between polls. */
static volatile uint8_t basic_mqtt_evt;
/** @brief MQTT event callback: record the latest event. */
static void basic_mqtt_event_cb(uint8_t e) { basic_mqtt_evt = e; }

/* Inbound capture for MQTTWAIT$: the last PUBLISH the broker delivered,
 * copied out of the transient callback buffers (which are only valid for
 * the callback's duration) into bounded static storage.  `rx_pending`
 * latches until the waiter consumes it. */
static volatile uint8_t basic_mqtt_rx_pending;
static char basic_mqtt_rx_topic[48];
static char basic_mqtt_rx_msg[TIKU_BASIC_MQTT_RX_CAP];
/** @brief MQTT message callback: copy a PUBLISH out and latch it pending. */
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

/**
 * @brief One pump step for the MQTT words.
 *
 * The shared shell pump (watchdog, WiFi drain, paced tcp_periodic, SLIP-aware
 * Ctrl-C; kernel/shell/tiku_shell_pump.c) with MQTT housekeeping at the paced
 * service point, after TCP, so MQTT sees the connection events TCP produced.
 *
 * @return 1 on Ctrl-C, else 0.
 */
static int
basic_net_mqtt_pump(void)
{
    return tiku_shell_pump_net(tiku_kits_net_mqtt_periodic);
}

/** @brief MQTTPUB "broker_ip", "topic", expr$: connect, publish, disconnect. */
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
        basic_throwf(TIKU_BASIC_ERR_NET, "bad broker IP '%s'", ipstr); return;
    }
    skip_ws(p);
    if (cur_peek(p) != ',') { basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return; }
    cur_advance(p);
    if (parse_path_literal(p, topic, sizeof(topic)) != 0) return;
    skip_ws(p);
    if (cur_peek(p) != ',') { basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return; }
    cur_advance(p);
    if (parse_strexpr(p, payload, sizeof(payload)) != 0) return;

    /* Initialise the TCP connection table. On a lean WiFi profile nothing
     * else does: the NET_TEST init and the SLIP net process are both absent
     * there, so without this mqtt_connect()'s tcp_connect() allocates from an
     * uninitialised table and never establishes. The init is idempotent, and
     * each MQTTPUB is a fresh connect/publish/disconnect. */
    tiku_kits_net_tcp_init();
    tiku_kits_net_mqtt_init();
    tiku_kits_net_mqtt_set_server(ip, 1883);
    tiku_kits_net_mqtt_set_credentials("tikubasic", (const char *)0,
                                       (const char *)0);
    basic_mqtt_evt = 0xFFu;
    if (tiku_kits_net_mqtt_connect(basic_mqtt_msg_cb, basic_mqtt_event_cb)
        != TIKU_KITS_NET_OK) {
        basic_throw(TIKU_BASIC_ERR_NET, "MQTT connect rejected (IP link up? 'wifi up')");
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
        basic_throw(TIKU_BASIC_ERR_NET, "MQTT connect timeout"); return;
    }
    tiku_kits_net_mqtt_publish(topic, (const uint8_t *)payload,
                               (uint16_t)strlen(payload), 0, 0);
    /* let the publish flush, then disconnect */
    deadline = (tiku_clock_time_t)(tiku_clock_time() + 2u * TIKU_CLOCK_SECOND);
    while (TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
        if (basic_net_mqtt_pump()) break;
    }
    tiku_kits_net_mqtt_disconnect();
    deadline = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND / 2);
    while (TIKU_CLOCK_LT(tiku_clock_time(), deadline)) (void)basic_net_mqtt_pump();
}

/**
 * @brief MQTTWAIT$ backend: wait up to @p secs for one PUBLISH on @p topic.
 *
 * Connect, subscribe, pump until a PUBLISH lands or the time is up, then
 * disconnect -- the MQTTPUB lifecycle, so no connection outlives the call.
 * The payload goes to @p out ("" on timeout).
 *
 * @return 0 if a message arrived, -1 otherwise; a bad IP or a failed connect
 *         also sets basic_error (category NET), and so does Ctrl-C (no
 *         category).
 */
static int
basic_net_mqtt_wait(const char *ipstr, const char *topic, long secs,
                    char *out, size_t cap)
{
    uint8_t ip[4];
    tiku_clock_time_t deadline;

    if (cap) out[0] = '\0';
    if (basic_net_parse_ip(ipstr, ip) != 0) {
        basic_throwf(TIKU_BASIC_ERR_NET, "bad broker IP '%s'", ipstr);
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
        basic_throw(TIKU_BASIC_ERR_NET, "MQTT connect rejected (IP link up? 'wifi up')");
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
        basic_throw(TIKU_BASIC_ERR_NET, "MQTT connect timeout"); return -1;
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
