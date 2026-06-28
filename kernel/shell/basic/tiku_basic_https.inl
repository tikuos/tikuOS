/*
 * tiku_basic_https.inl - HTTPS GET backend for BASIC HTTPGET$.
 *
 * Drives the certificate-based TLS 1.3 client (tikukits/crypto/tls13) over
 * the TikuOS TCP stack to fetch a page from a real https:// server.  The
 * send/recv callbacks busy-wait while pumping the WiFi RX drain + TCP timers
 * (the cooperative-blocking pattern shared with MQTTPUB), so the console
 * stays alive and the cyw43 RX path keeps flowing during the handshake.
 *
 * Trust anchor: ISRG Root X1 (Let's Encrypt), baked in below.  Certificate
 * validity dates are currently skipped (now=0) until a wall clock (NTP/RTC)
 * feeds the verifier.
 */

#if TIKU_BASIC_NET_ENABLE && (TIKU_KITS_NET_HTTP_ENABLE + 0)

#include "tiku_basic_https_roots.inl"  /* tiku_https_roots[] + TIKU_HTTPS_NROOTS */
#include <tikukits/net/tls/tls12/tiku_kits_crypto_tls12.h>  /* TLS 1.2 fallback */

static int basic_net_parse_ip(const char *s, uint8_t out[4]); /* in tiku_basic_net.inl */

static int basic_http_status;          /* last HTTPGET$ status */


#if defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_trng_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_trng_arch.h>
#endif

static void
basic_https_rng(uint8_t *b, size_t n)
{
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
    (void)tiku_trng_arch_read_bytes(b, n);   /* on-die hardware TRNG */
#else
    size_t i;                          /* no HW TRNG (weak -- dev builds only) */
    for (i = 0; i < n; i++) b[i] = (uint8_t)(tiku_clock_time() >> (i & 7));
#endif
}

/* Milestone hook: kick the watchdog at each handshake step so a legitimately
 * slow handshake survives while a genuine hang still trips the WDT. */
static void basic_tls13_dbg(const char *m)
{
    (void)m;
    tiku_watchdog_kick();
}

/* One pump step: deliver inbound packets every call (fast, no timers), but pace
 * tcp_periodic to ~8 Hz -- it advances connect/retransmit timeouts per call,
 * so a tight loop calling it every iteration would blow through them (the
 * same trap as dns_poll).
 *
 * RX delivery is transport-specific. On WiFi the radio RX is a separate channel
 * (cyw43 SPI), drained via whd_drain_rx(); the console UART is unrelated, so we
 * also drop a stray keystroke that would otherwise pile up. On a SLIP build the
 * console UART *is* the IP transport, and the shell loop that normally runs the
 * shared RX demux is blocked inside this builtin -- so we drive that same demux
 * here via tiku_shell_net_pump().  It must be the shell's demux (not a private
 * slip_poll_rx loop): a SLIP frame trickles in at the line rate over several ms,
 * far slower than this is polled, so a caller-local accumulator gets reset
 * between calls and shreds the frame into 1-byte garbage.  The shell demux keeps
 * its frame buffer in static state, so it reassembles correctly.  We also must
 * NOT read via the console getc, which would discard the SLIP bytes.  Without
 * this, HTTPGET$ over SLIP never sees a single reply (DNS / SYN-ACK / TLS). */
static void
basic_https_pump(void)
{
    static tiku_clock_time_t last_tcp;
    tiku_clock_time_t now = tiku_clock_time();
    tiku_watchdog_kick();
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
    (void)whd_drain_rx();
    if (tiku_shell_io_rx_ready()) (void)tiku_shell_io_getc();
#elif TIKU_SHELL_CMD_SLIP
    tiku_shell_net_pump();          /* shell's persistent SLIP demux -> ipv4_input */
#endif
    if ((tiku_clock_time_t)(now - last_tcp) >= (tiku_clock_time_t)(TIKU_CLOCK_SECOND / 8)) {
        last_tcp = now;
        tiku_kits_net_tcp_periodic();
    }
}

static volatile uint8_t basic_https_evt;
static void basic_https_on_evt(tiku_kits_net_tcp_conn_t *c, uint8_t e){ (void)c; basic_https_evt = e; }
static void basic_https_on_rx (tiku_kits_net_tcp_conn_t *c, uint16_t a){ (void)c; (void)a; }

#define BASIC_HTTPS_DEADLINE() \
    ((tiku_clock_time_t)(tiku_clock_time() + 20u * TIKU_CLOCK_SECOND))
#define BASIC_HTTPS_EXPIRED(dl)  (!TIKU_CLOCK_LT(tiku_clock_time(), (dl)))

static int
basic_https_send(void *ctx, const uint8_t *b, size_t n)
{
    tiku_kits_net_tcp_conn_t *c = ctx;
    size_t off = 0;
    tiku_clock_time_t dl = BASIC_HTTPS_DEADLINE();
    while (off < n) {
        /* tcp_send transmits exactly ONE segment and rejects data_len >
         * snd_mss; chunk by the negotiated MSS (88 over SLIP, larger on WiFi),
         * never a fixed 512 -- otherwise on a small-MTU link every chunk
         * exceeds snd_mss, tcp_send returns OVERFLOW forever, and only sub-MSS
         * writes (e.g. the 5-byte TLS record header) ever go out while the
         * record body is silently dropped until the deadline. */
        uint16_t mss = c->snd_mss ? c->snd_mss : TIKU_KITS_NET_TCP_MSS;
        size_t chunk = n - off; if (chunk > mss) chunk = mss;
        if (tiku_kits_net_tcp_send(c, b + off, (uint16_t)chunk) == TIKU_KITS_NET_OK)
            off += chunk;
        basic_https_pump();
        if (BASIC_HTTPS_EXPIRED(dl)) return -1;
    }
    return (int)n;
}

static int
basic_https_recv(void *ctx, uint8_t *b, size_t n)
{
    tiku_kits_net_tcp_conn_t *c = ctx;
    uint16_t want = (uint16_t)(n > 0xFFFFu ? 0xFFFFu : n);
    tiku_clock_time_t dl = BASIC_HTTPS_DEADLINE();
    for (;;) {
        uint16_t got = tiku_kits_net_tcp_read(c, b, want);
        if (got > 0) return (int)got;
        if (basic_https_evt == TIKU_KITS_NET_TCP_EVT_ABORTED) return -1;
        if (basic_https_evt == TIKU_KITS_NET_TCP_EVT_CLOSED) {
            got = tiku_kits_net_tcp_read(c, b, want);
            return got > 0 ? (int)got : -1;
        }
        basic_https_pump();
        if (BASIC_HTTPS_EXPIRED(dl)) return -1;
    }
}

/*
 * HTTPGET$ backend.  Returns body length (>= 0) into out[0..cap-1] (NUL-
 * terminated) and sets basic_http_status, or -1 on any failure.
 */
/* Open a TCP connection to ip:443 and block (pumping) until CONNECTED.
 * Returns the conn or NULL; reused for the initial attempt and the 1.2 retry. */
static tiku_kits_net_tcp_conn_t *
basic_https_open(const uint8_t ip[4], uint16_t src_port)
{
    tiku_kits_net_tcp_conn_t *tcp;
    tiku_clock_time_t dl;
    basic_https_evt = 0;
    tcp = tiku_kits_net_tcp_connect(ip, 443, src_port,
                                    basic_https_on_rx, basic_https_on_evt);
    if (tcp == NULL) return NULL;
    dl = BASIC_HTTPS_DEADLINE();
    while (basic_https_evt != TIKU_KITS_NET_TCP_EVT_CONNECTED) {
        if (basic_https_evt == TIKU_KITS_NET_TCP_EVT_ABORTED) return NULL;
        basic_https_pump();
        if (BASIC_HTTPS_EXPIRED(dl)) { tiku_kits_net_tcp_abort(tcp); return NULL; }
    }
    return tcp;
}

static int
basic_https_get(const char *host, const char *path, char *out, size_t cap)
{
    static tiku_kits_crypto_tls13_conn_t tls;       /* ~16 KB: keep off-stack */
    static tiku_kits_crypto_tls12_conn_t tls12;     /* TLS 1.2 fallback state */
    tiku_kits_crypto_tls13_io_t io;
    tiku_kits_net_tcp_conn_t   *tcp;
    uint8_t  ip[4];
    char     req[256];
    size_t   total = 0, rl;
    int      n, use12 = 0;
    tiku_clock_time_t dl;

    basic_http_status = 0;

    /* Initialise the TCP table: on a lean WiFi build nothing else does (the
     * NET_TEST init + SLIP net process are absent), so tcp_connect() would
     * otherwise allocate from an uninitialised table (same as BASIC MQTTPUB). */
    tiku_kits_net_tcp_init();

    /* resolve host (literal dotted-quad accepted directly) */
    if (basic_net_parse_ip(host, ip) != 0) {
        static const uint8_t dnssrv[4] = {8, 8, 8, 8};   /* Google public DNS */
        int8_t drc;
        tiku_kits_net_dns_init();
        tiku_kits_net_dns_set_server(dnssrv);
        tiku_clock_time_t np;
        drc = tiku_kits_net_dns_resolve(host);
        if (drc != TIKU_KITS_NET_OK) {
            SHELL_PRINTF(SH_RED "? HTTPGET: DNS error\n" SH_RST); return -1; }
        dl = BASIC_HTTPS_DEADLINE();
        np = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND);
        for (;;) {
            /* Pump every iteration to deliver the WiFi RX (the DNS reply),
             * but call dns_poll only ~1 Hz: each poll without a reply counts
             * as a retry toward its timeout, so a tight loop would exhaust the
             * retry budget in milliseconds. */
            basic_https_pump();
            if (!TIKU_CLOCK_LT(tiku_clock_time(), np)) {
                np = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND);
                tiku_kits_net_dns_poll();
                if (tiku_kits_net_dns_get_state() == TIKU_KITS_NET_DNS_STATE_DONE) break;
                if (tiku_kits_net_dns_get_state() == TIKU_KITS_NET_DNS_STATE_ERROR) {
                    SHELL_PRINTF("[https] dns state ERROR\n"); return -1; }
            }
            if (BASIC_HTTPS_EXPIRED(dl)) {
                SHELL_PRINTF("[https] dns timeout\n"); tiku_kits_net_dns_abort(); return -1; }
        }
        tiku_kits_net_dns_get_addr(ip);
    }

    /* TCP connect :443 */
    tcp = basic_https_open(ip, 49152);
    if (tcp == NULL) { SHELL_PRINTF(SH_RED "? HTTPGET: TCP connect failed\n" SH_RST); return -1; }
    (void)dl;

    /* TLS 1.3 cert handshake (validity skipped until NTP/RTC wired).  The
     * milestone hook (basic_tls13_dbg) kicks the watchdog at each step so a
     * legitimately slow handshake survives while a genuine hang still trips
     * the WDT for recovery -- do NOT pause the WDT here (that makes a hang
     * unrecoverable). */
    io.send = basic_https_send; io.recv = basic_https_recv; io.ctx = tcp;
    tiku_kits_crypto_tls13_dbg = basic_tls13_dbg;
    /* now_unix from the RTC: enforces cert validity windows once a clock is
     * set (NTP/SETTIME); 0 until then, which skips the date check.  Try TLS 1.3
     * first; on failure fall back to TLS 1.2 (the 1.2-only tail) on a fresh
     * connection, since the ServerHello has already been consumed. */
    {
        uint64_t now = (uint64_t)tiku_rtc_get_seconds();
        if (tiku_kits_crypto_tls13_connect(&io, basic_https_rng, host,
                                           tiku_https_roots, TIKU_HTTPS_NROOTS,
                                           now, &tls) != 0) {
            tiku_kits_net_tcp_close(tcp);
            tcp = basic_https_open(ip, 49153);
            if (tcp == NULL) {
                SHELL_PRINTF(SH_RED "? HTTPGET: TCP connect failed\n" SH_RST);
                return -1;
            }
            io.ctx = tcp;
            if (tiku_kits_crypto_tls12_connect(&io, basic_https_rng, host,
                                               tiku_https_roots, TIKU_HTTPS_NROOTS,
                                               now, &tls12) != 0) {
                SHELL_PRINTF(SH_RED
                    "? HTTPGET: TLS handshake/cert validation failed\n" SH_RST);
                tiku_kits_net_tcp_close(tcp);
                return -1;
            }
            use12 = 1;
        }
    }

    /* GET <path> HTTP/1.0 */
    req[0] = '\0';
    strcat(req, "GET ");  strcat(req, path);
    strcat(req, " HTTP/1.0\r\nHost: "); strcat(req, host);
    strcat(req, "\r\nConnection: close\r\n\r\n");
    rl = strlen(req);
    n = use12 ? tiku_kits_crypto_tls12_write(&tls12, (const uint8_t *)req, rl)
              : tiku_kits_crypto_tls13_write(&tls,  (const uint8_t *)req, rl);
    if (n < 0) {
        tiku_kits_net_tcp_close(tcp);
        return -1;
    }

    /* read the response body into out[] */
    for (;;) {
        if (total + 1 >= cap) break;
        n = use12
            ? tiku_kits_crypto_tls12_read(&tls12, (uint8_t *)out + total, cap - 1 - total)
            : tiku_kits_crypto_tls13_read(&tls,   (uint8_t *)out + total, cap - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    out[total] = '\0';

    /* parse "HTTP/1.x NNN" status line */
    if (total > 12 && out[0] == 'H') {
        const char *sp = out;
        while (*sp && *sp != ' ') sp++;
        if (*sp == ' ' && sp[1] && sp[2] && sp[3])
            basic_http_status = (sp[1]-'0')*100 + (sp[2]-'0')*10 + (sp[3]-'0');
    }

    tiku_kits_net_tcp_close(tcp);
    return (int)total;
}

#endif /* TIKU_BASIC_NET_ENABLE && HTTP */
