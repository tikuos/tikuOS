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
#include <tikukits/net/http/tiku_kits_net_http.h>  /* shared cert HTTPS engine */

static int basic_net_parse_ip(const char *s, uint8_t out[4]); /* in tiku_basic_net.inl */

static int basic_http_status;          /* last HTTPGET$/HTTPPOST$ status */
static char basic_http_hdrs[TIKU_BASIC_HTTP_HDRS_MAX];  /* extra request headers
                                        * set by HTTPHEADER ("Name: value\r\n"...); "" = none */

/* Compile-time budget for req[] in basic_https_get(): the four bounded inputs
 * (host + path + HTTPHEADER block + content-type) plus a generous fixed
 * allowance (128) for the method, the HTTP/Host/Connection/Content-* tokens, the
 * decimal Content-Length and the CRLFs.  Bumping any cap in tiku_basic_config.h
 * without growing REQ_MAX breaks the build here instead of overflowing req[]. */
_Static_assert(TIKU_BASIC_HTTP_HOST_MAX + TIKU_BASIC_HTTP_PATH_MAX +
               TIKU_BASIC_HTTP_HDRS_MAX + TIKU_BASIC_HTTP_CTYPE_MAX + 128u
               <= TIKU_BASIC_HTTP_REQ_MAX,
               "BASIC HTTP request buffer too small for its bounded inputs");


#if defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_trng_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_trng_arch.h>
#endif

#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
#include <tikukits/crypto/hmac/tiku_kits_crypto_hmac.h>

/*
 * RNG for the TLS handshake: an HMAC-DRBG (NIST SP 800-90A) seeded ONCE from
 * the on-die hardware TRNG, then expanded in software.
 *
 * Why not read the TRNG directly per handshake (what this replaced): the
 * CryptoCell-312 / RP2350 ring-oscillator TRNG is slow -- a ClientHello's worth
 * of entropy (32-byte client random + the 32-byte P-256 ECDHE private key)
 * drains the 24-byte EHR cache several times, and each refill is a blocking
 * ring-oscillator fill with health-test re-arms. Measured at 2.5-16 s on
 * Apollo510. Because the builtin pumps the net cooperatively, that block also
 * stalls TCP ACKs, so the handshake overran the server's ~10 s timeout and the
 * peer RST it -- about half of back-to-back fetches failed purely on timing
 * luck (tun0 pcap: handshake completes, then 5-16 s of silence before the
 * ClientHello, peer FIN/RST with ack=1 = it received zero bytes).
 *
 * Fix: pay the slow TRNG once to seed the DRBG (basic_https_rng_prepare(),
 * called before the TCP connect so nothing is waiting on the handshake), then
 * every ClientHello pulls from the DRBG in microseconds. Reseed only every
 * DRBG_RESEED_INTERVAL generates (also pre-connect) for forward secrecy -- in
 * practice never within a browsing session.
 */
#define DRBG_SEED_BYTES       48u     /* >=256-bit entropy + nonce margin */
#define DRBG_RESEED_INTERVAL  4096u   /* generates between reseeds (rare) */

static uint8_t  drbg_K[32];
static uint8_t  drbg_V[32];
static uint8_t  drbg_ready;
static uint32_t drbg_reseed_ctr;

/* HMAC-SHA256 into an aliasing-safe temp, so `out` may equal `key` or `data`. */
static void drbg_hmac(const uint8_t *key, const uint8_t *data, uint16_t dlen,
                      uint8_t out[32])
{
    uint8_t tmp[32];
    (void)tiku_kits_crypto_hmac_sha256(key, 32u, data, dlen, tmp);
    memcpy(out, tmp, 32u);
}

/* HMAC_DRBG Update (SP 800-90A 10.1.2.2). pd may be NULL when pd_len == 0. */
static void drbg_update(const uint8_t *pd, uint16_t pd_len)
{
    uint8_t buf[32u + 1u + DRBG_SEED_BYTES];      /* V || tag || provided_data */
    memcpy(buf, drbg_V, 32u);
    buf[32] = 0x00u;
    if (pd_len) memcpy(buf + 33, pd, pd_len);
    drbg_hmac(drbg_K, buf, (uint16_t)(33u + pd_len), drbg_K);   /* K */
    drbg_hmac(drbg_K, drbg_V, 32u, drbg_V);                     /* V */
    if (pd_len) {
        memcpy(buf, drbg_V, 32u);
        buf[32] = 0x01u;
        memcpy(buf + 33, pd, pd_len);
        drbg_hmac(drbg_K, buf, (uint16_t)(33u + pd_len), drbg_K);
        drbg_hmac(drbg_K, drbg_V, 32u, drbg_V);
    }
}

/* The only place the slow hardware TRNG is read: gather a fresh seed and
 * (re)key the DRBG, then wipe the transient entropy from the stack. */
static void drbg_reseed(void)
{
    uint8_t seed[DRBG_SEED_BYTES];
    size_t  i;
    if (tiku_trng_arch_read_bytes(seed, sizeof seed) != TIKU_TRNG_OK) {
        /* Healthy hardware never lands here; if the TRNG faults, mix the clock
         * so we at least don't reuse a fixed state rather than hanging. */
        for (i = 0; i < sizeof seed; i++)
            seed[i] ^= (uint8_t)(tiku_clock_time() >> ((i & 3u) * 8u));
    }
    drbg_update(seed, (uint16_t)sizeof seed);
    for (i = 0; i < sizeof seed; i++) seed[i] = 0u;   /* wipe raw entropy */
    drbg_reseed_ctr = 0u;
}

/* Seed-if-needed; call before opening the connection. Slow only the first time
 * and on the rare reseed boundary -- both with no server waiting. */
static void basic_https_rng_prepare(void)
{
    if (!drbg_ready) {
        size_t i;
        for (i = 0; i < 32u; i++) { drbg_K[i] = 0x00u; drbg_V[i] = 0x01u; }
        drbg_ready = 1u;
        drbg_reseed();
    } else if (drbg_reseed_ctr >= DRBG_RESEED_INTERVAL) {
        drbg_reseed();
    }
}

/* The TLS RNG callback: HMAC-DRBG generate. Never touches the TRNG, so it is
 * always microseconds and never stalls a live handshake. */
static void basic_https_rng(uint8_t *b, size_t n)
{
    size_t off = 0u;
    if (!drbg_ready) basic_https_rng_prepare();   /* defensive */
    while (off < n) {
        size_t take = (n - off < 32u) ? (n - off) : 32u;
        drbg_hmac(drbg_K, drbg_V, 32u, drbg_V);   /* V = HMAC(K, V) */
        memcpy(b + off, drbg_V, take);
        off += take;
    }
    drbg_update((const uint8_t *)0, 0u);          /* post-generate update */
    drbg_reseed_ctr++;
}
#else
static void basic_https_rng_prepare(void) { /* no HW TRNG: nothing to seed */ }

static void
basic_https_rng(uint8_t *b, size_t n)
{
    size_t i;                          /* no HW TRNG (weak -- dev builds only) */
    for (i = 0; i < n; i++) b[i] = (uint8_t)(tiku_clock_time() >> (i & 7));
}
#endif

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

/* Human label for a tiku_kits_crypto_tls13_last_stage code (see the header). */
static const char *
basic_tls_stage_str(int s)
{
    switch (s) {
    case -2:  return "ServerHello read";
    case -3:  return "ServerHello bad";
    case -5:  return "server-flight read (transport)";
    case -6:  return "unexpected record";
    case -7:  return "decrypt (corrupt flight)";
    case -9:  return "cert parse";
    case -10: return "cert-verify";
    case -11: return "chain untrusted";
    case -12: return "Finished";
    case -13: return "flight buffer overflow";
    case -14: return "client Finished send";
    default:  return s >= 1 ? "got past cert checks" : "unknown";
    }
}

/* --------------------------------------------------------------------------
 * Heavy-crypto offload onto a worker thread (threads phase-1).
 *
 * The handshake's CPU-bound public-key ops (ECDHE, CertVerify, cert-chain
 * verify) used to run inline on the shell thread -- tens to hundreds of ms
 * each during which NOTHING pumped the net (the peer could RST -- the
 * apollo510 half-fail class) and no kernel timer or rule was serviced.  When
 * worker threads are available we install io.offload: the handshake runs each
 * of those ops on ONE dedicated worker while this drive loop keeps the net
 * pumped and dispatches the rest of the kernel's processes.  With threads off
 * (or the knob cleared) io.offload stays NULL and the handshake is exactly as
 * before -- the tikukits change is additive.
 * ------------------------------------------------------------------------- */
#ifndef TIKU_BASIC_HTTPS_OFFLOAD
#  if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
#    define TIKU_BASIC_HTTPS_OFFLOAD 1
#  else
#    define TIKU_BASIC_HTTPS_OFFLOAD 0
#  endif
#endif

#if TIKU_BASIC_HTTPS_OFFLOAD
#include <kernel/threads/tiku_thread.h>
#include <kernel/process/tiku_process.h>
#include <hal/tiku_cpu.h>

/* One dedicated crypto worker.  8 KB carries the cert-chain DER parse + the
 * RSA/ECDSA verify call chain; the bignums live in the primitives' own static
 * scratch, not on this stack. */
TIKU_THREAD(basic_crypto_worker, 8192);

static int  (* volatile basic_crypto_fn)(void *);
static void *  volatile basic_crypto_arg;
static volatile int      basic_crypto_rc;
static volatile uint8_t  basic_crypto_busy;   /* crypto in flight -> no nesting */

static void basic_crypto_worker_body(void *arg)
{
    (void)arg;
    basic_crypto_rc = basic_crypto_fn(basic_crypto_arg);
}

/*
 * io.offload: run @p fn (a pure handshake-crypto closure over connect()'s
 * still-live stack) on the worker while keeping the kernel alive.  The drive
 * loop mirrors the scheduler idle branch -- pump the net, dispatch every ready
 * process EXCEPT our own (tiku_process_run_except so a queued shell event can't
 * recursively re-enter this very command), then hand the CPU to the worker
 * until an event wakes us.  tiku_current_process, which call_process() clears
 * as it fans out, is restored before returning to the handshake.  Any failure
 * to start the worker falls back to running @p fn inline -- byte-identical to
 * the no-offload path -- so correctness never depends on the worker.
 */
static int basic_https_offload(int (*fn)(void *), void *arg)
{
    struct tiku_process *owner = tiku_current_process;
    tiku_clock_time_t dl;

    if (basic_crypto_busy) {         /* non-reentrant primitives: never overlap */
        return fn(arg);
    }
    basic_crypto_fn  = fn;
    basic_crypto_arg = arg;
    basic_crypto_rc  = 0;
    basic_crypto_busy = 1;

    if (tiku_thread_start(&basic_crypto_worker,
                          basic_crypto_worker_body, 0) != 0) {
        basic_crypto_busy = 0;
        return fn(arg);              /* worker unavailable -> inline, identical */
    }

    dl = BASIC_HTTPS_DEADLINE();
    while (basic_crypto_worker.state != TIKU_THREAD_DONE) {
        basic_https_pump();                          /* net + WDT stay alive  */
        while (tiku_process_run_except(owner)) { }   /* others' timers/rules  */
        tiku_atomic_enter();
        if (tiku_process_queue_empty() && tiku_thread_worker_ready()) {
            tiku_thread_kernel_block();               /* CPU -> the crypto     */
        }
        tiku_atomic_exit();
        if (BASIC_HTTPS_EXPIRED(dl)) {                /* safety: never wedge   */
            break;
        }
    }

    tiku_current_process = owner;    /* the drain cleared it via call_process  */
    basic_crypto_busy = 0;
    return basic_crypto_rc;
}
#endif /* TIKU_BASIC_HTTPS_OFFLOAD */

/* --------------------------------------------------------------------------
 * Adapters that let basic_https_get() drive the shared kit HTTPS engine
 * (tiku_kits_net_http_cert_exchange) over BASIC's own proven transport: a
 * reconnect for the TLS 1.2 fallback and a sink that collects the response.
 * ------------------------------------------------------------------------- */

/* Reconnect: close the spent socket and reopen a fresh 4-tuple (src+1) -- the
 * same reopen the old inline fallback did. */
struct basic_https_rc_ctx { const uint8_t *ip; uint16_t src; };
static void *
basic_https_reconnect(void *c, void *old)
{
    struct basic_https_rc_ctx *x = c;
    tiku_kits_net_tcp_close((tiku_kits_net_tcp_conn_t *)old);
    return basic_https_open(x->ip, (uint16_t)(x->src + 1));
}

/* Sink: append decrypted bytes into out[] up to cap-1, mirroring the old inline
 * read loop's "stop when the buffer is full" behaviour. */
struct basic_https_sink_ctx { char *out; size_t cap; size_t total; };
static uint8_t
basic_https_sink(void *c, const uint8_t *d, uint16_t len)
{
    struct basic_https_sink_ctx *s = c;
    uint16_t i;
    for (i = 0; i < len && s->total + 1 < s->cap; i++) {
        s->out[s->total++] = (char)d[i];
    }
    return (uint8_t)(s->total + 1 < s->cap);   /* 0 = full -> stop reading */
}

static int
basic_https_get(const char *method, const char *host, const char *path,
                const char *body, const char *ctype, char *out, size_t cap)
{
    /* TLS connection state now lives inside the shared kit engine
     * (tiku_kits_net_http_cert_exchange), not here. */
    tiku_kits_crypto_tls13_io_t io;
    tiku_kits_net_tcp_conn_t   *tcp;
    uint8_t  ip[4];
    char     req[TIKU_BASIC_HTTP_REQ_MAX];  /* method+path+host+HTTPHEADER+POST hdrs; budget _Static_assert'd above */
    size_t   total = 0, rl;
    tiku_clock_time_t dl;
    static uint16_t src_seq = 49150;     /* fresh ephemeral port pair per call */

    basic_http_status = 0;
#if TIKU_BASIC_HTTPS_OFFLOAD
    /* Refuse a fetch triggered from a process the crypto drive loop dispatched
     * (a rule/timer that fetches while another fetch's crypto is in flight):
     * the crypto primitives are non-reentrant, so a nested handshake would
     * corrupt the worker's in-flight state.  Serial fetches only. */
    if (basic_crypto_busy) {
        SHELL_PRINTF(SH_RED "? HTTPGET: busy (crypto in flight)\n" SH_RST);
        return -1;
    }
#endif
    /* Advance the source port every call so a redirect refetch to the SAME
     * server IP (e.g. host -> www.host sharing one Cloudflare anycast IP)
     * doesn't reuse the just-closed connection's 4-tuple (TIME_WAIT) and get
     * its SYN dropped -- which showed as "TCP connect failed". */
    src_seq = (src_seq >= 60000u) ? 49152u : (uint16_t)(src_seq + 2);

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

    /* Seed the TLS RNG (HMAC-DRBG) before opening the connection: the one-time
     * hardware-TRNG gather is slow (seconds), and doing it here means no peer
     * is waiting on the handshake while it runs.  After this, every ClientHello
     * draws randomness from the DRBG in microseconds. */
    basic_https_rng_prepare();

    /* TCP connect :443 */
    tcp = basic_https_open(ip, src_seq);
    if (tcp == NULL) { SHELL_PRINTF(SH_RED "? HTTPGET: TCP connect failed\n" SH_RST); return -1; }
    (void)dl;

    /* Build the request up front (independent of the negotiated TLS version):
     * <METHOD> <path> HTTP/1.0 + Host + any HTTPHEADER lines, and -- for a body
     * (POST) -- Content-Type + Content-Length.  The body follows as a second
     * TLS record so it need not fit in req[]. */
    req[0] = '\0';
    strcat(req, method); strcat(req, " "); strcat(req, path);
    strcat(req, " HTTP/1.0\r\nHost: "); strcat(req, host);
    strcat(req, "\r\nConnection: close\r\n");
    if (basic_http_hdrs[0]) strcat(req, basic_http_hdrs);   /* each line ends \r\n */
    if (body) {
        char cl[16], tmp[16]; size_t bl = strlen(body); int ci = 0, ti = 0;
        strcat(req, "Content-Type: ");
        strcat(req, (ctype && ctype[0]) ? ctype : "application/json");
        strcat(req, "\r\nContent-Length: ");
        if (bl == 0) cl[ci++] = '0';
        else { do { tmp[ti++] = (char)('0' + (int)(bl % 10)); bl /= 10; } while (bl && ti < 15);
               while (ti > 0) cl[ci++] = tmp[--ti]; }
        cl[ci] = '\0';
        strcat(req, cl);
        strcat(req, "\r\n");
    }
    strcat(req, "\r\n");                                     /* end of headers */
    rl = strlen(req);

    /* Hand the connected socket to the shared kit engine: it runs the TLS 1.3
     * cert handshake over our transport (basic_https_send/recv), falls back to
     * TLS 1.2 on a fresh connection via basic_https_reconnect (the ServerHello
     * is consumed on a 1.3 failure), sends the request + body, and streams the
     * response into out[] through basic_https_sink.
     *
     * now_unix gates cert validity: set only after an explicit SETTIME/NTP
     * (tiku_rtc_is_set), else 0 to skip the date window -- signature + trust
     * anchor + hostname stay enforced.  Gate on is_set(), NOT a bare
     * get_seconds(): once tiku_rtc_init() has stamped the soft-RTC gate (any
     * prior boot; on Ambiq it survives in MRAM across reflashes), get_seconds()
     * returns a small non-zero boot-uptime value that would make every live
     * cert "not yet valid" (stage -11) for every site.
     *
     * Do NOT pause the WDT: basic_tls13_dbg kicks it per handshake step so a
     * legitimately slow handshake survives while a genuine hang still trips the
     * WDT for recovery. */
    io.send = basic_https_send; io.recv = basic_https_recv; io.ctx = tcp;
#if TIKU_BASIC_HTTPS_OFFLOAD
    io.offload = basic_https_offload;   /* run heavy crypto on the worker */
#else
    io.offload = NULL;                  /* inline (default portable path)  */
#endif
    tiku_kits_crypto_tls13_dbg = basic_tls13_dbg;
    {
        struct basic_https_rc_ctx   rcx = { ip, src_seq };
        struct basic_https_sink_ctx scx = { out, cap, 0 };
        tiku_kits_net_http_tls_t    tconf;
        int8_t erc;

        tconf.trust    = TIKU_KITS_NET_HTTP_CERT;
        tconf.roots    = tiku_https_roots;
        tconf.nroots   = TIKU_HTTPS_NROOTS;
        tconf.rng      = basic_https_rng;
        tconf.now_unix = tiku_rtc_is_set() ? (uint64_t)tiku_rtc_get_seconds() : 0;
        tconf.offload  = NULL;              /* offload rides on io.offload */

        erc = tiku_kits_net_http_cert_exchange(
            &io, basic_https_reconnect, &rcx, &tconf, host,
            (const uint8_t *)req, (uint16_t)rl,
            (const uint8_t *)body, (uint16_t)(body ? strlen(body) : 0),
            basic_https_sink, &scx);

        tcp   = io.ctx;                     /* engine may have reconnected */
        total = scx.total;

        if (erc != TIKU_KITS_NET_OK) {
            if (erc == TIKU_KITS_NET_ERR_HTTP_TCP) {
                SHELL_PRINTF(SH_RED
                    "? HTTPGET: TCP connect failed\n" SH_RST);
            } else {
                int      st = tiku_kits_crypto_tls13_last_stage;
                uint32_t rx = tiku_kits_crypto_tls13_last_rx;
                int      ev = basic_https_evt;   /* RST? closed? silent? */
                SHELL_PRINTF(SH_RED
                    "? HTTPGET: TLS failed -- tls1.3 stage %d (%s), %u B in, "
                    "link=%s\n" SH_RST,
                    st, basic_tls_stage_str(st), (unsigned)rx,
                    (ev == TIKU_KITS_NET_TCP_EVT_ABORTED ? "RST" :
                     ev == TIKU_KITS_NET_TCP_EVT_CLOSED  ? "closed" :
                     "silent"));
            }
            if (tcp) tiku_kits_net_tcp_close(tcp);
            return -1;
        }
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
