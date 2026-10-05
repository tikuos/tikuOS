/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_https.inl - HTTPS client backend for BASIC.
 *
 * Drives the http kit's certificate engine (TLS 1.3, falling back to 1.2) over
 * the TCP stack.  The send and receive callbacks pump the radio drain and TCP
 * timers while they wait, so the console and the receive path keep running.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_NET_ENABLE && (TIKU_KITS_NET_HTTP_ENABLE + 0)

#include "tiku_basic_https_roots.inl"  /* trust store from /data/roots.bin */
#include <tikukits/net/tls/tls12/tiku_kits_crypto_tls12.h>  /* TLS 1.2 */
#include <tikukits/net/http/tiku_kits_net_http.h>  /* cert HTTPS engine */

/* Defined in tiku_basic_net.inl. */
static int basic_net_parse_ip(const char *s, uint8_t out[4]);

/** Status code of the last HTTPS request, read by HTTPSTATUS(). */
static int basic_http_status;
/** Extra request headers from HTTPHEADER ("Name: value\r\n"...), or "". */
static char basic_http_hdrs[TIKU_BASIC_HTTP_HDRS_MAX];

/* Compile-time budget for req[] in basic_https_get(): the four bounded inputs
 * (host + path + HTTPHEADER block + content-type) plus a fixed allowance (128)
 * for the method, the HTTP/Host/Connection/Content-* tokens, the decimal
 * Content-Length and the CRLFs.  Raising a cap in tiku_basic_config.h without
 * growing REQ_MAX fails this assert. */
_Static_assert(TIKU_BASIC_HTTP_HOST_MAX + TIKU_BASIC_HTTP_PATH_MAX +
               TIKU_BASIC_HTTP_HDRS_MAX + TIKU_BASIC_HTTP_CTYPE_MAX + 128u
               <= TIKU_BASIC_HTTP_REQ_MAX,
               "BASIC HTTP request buffer too small for its bounded inputs");


#if defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_trng_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_trng_arch.h>
#elif defined(PLATFORM_MSP430)
#include <arch/msp430/tiku_trng_arch.h>
#elif defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_trng_arch.h>            /* CRACEN ring-osc TRNG */
#elif defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_trng_arch.h>
#elif defined(PLATFORM_RA8P1)
#include <arch/ra8p1/tiku_trng_arch.h>
#elif defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_trng_arch.h>
#endif

#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
    defined(PLATFORM_MSP430) || defined(PLATFORM_NORDIC) || \
    defined(PLATFORM_STM32N6) || defined(PLATFORM_RA8P1) || \
    defined(PLATFORM_ESP32C61)
#include <tikukits/crypto/hmac/tiku_kits_crypto_hmac.h>

/*
 * RNG for the TLS handshake: an HMAC-DRBG (NIST SP 800-90A) seeded once from
 * the platform TRNG, then expanded in software.  The TRNG is slow -- on Ambiq
 * and RP2350 a ClientHello's worth of entropy (the 32-byte client random and
 * the 32-byte P-256 ECDHE private key) takes several blocking refills -- and
 * the builtin pumps the net cooperatively, so reading it mid-handshake stalls
 * the TCP ACKs until the server times the handshake out.
 *
 * basic_https_rng_prepare() therefore seeds the DRBG before the TCP connect,
 * when no server is waiting, and every ClientHello draws from the DRBG.  It
 * reseeds every DRBG_RESEED_INTERVAL generates, also before a connect, for
 * forward secrecy.
 */
#define DRBG_SEED_BYTES       48u     /* >=256-bit entropy + nonce margin */
#define DRBG_RESEED_INTERVAL  4096u   /* generates between reseeds (rare) */

static uint8_t  drbg_K[32];
static uint8_t  drbg_V[32];
static uint8_t  drbg_ready;
static uint32_t drbg_reseed_ctr;

/**
 * @brief HMAC-SHA256 through a temporary, so @p out may alias @p key or
 *        @p data.
 */
static void drbg_hmac(const uint8_t *key, const uint8_t *data, uint16_t dlen,
                      uint8_t out[32])
{
    uint8_t tmp[32];
    (void)tiku_kits_crypto_hmac_sha256(key, 32u, data, dlen, tmp);
    memcpy(out, tmp, 32u);
}

/**
 * @brief HMAC_DRBG Update (SP 800-90A 10.1.2.2).
 * @param pd      Provided data; may be NULL when @p pd_len is 0.
 * @param pd_len  Length of @p pd.
 */
static void drbg_update(const uint8_t *pd, uint16_t pd_len)
{
    uint8_t buf[32u + 1u + DRBG_SEED_BYTES];  /* V || tag || provided_data */
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

/**
 * @brief Gather a fresh seed from the TRNG and rekey the DRBG.
 *
 * The only place the TRNG is read; the raw entropy is wiped afterwards.
 */
static void drbg_reseed(void)
{
    uint8_t seed[DRBG_SEED_BYTES];
    size_t  i;
    if (tiku_trng_arch_read_bytes(seed, sizeof seed) != TIKU_TRNG_OK) {
        /* On a TRNG fault, mix the clock into the seed so the state still
         * changes. */
        for (i = 0; i < sizeof seed; i++)
            seed[i] ^= (uint8_t)(tiku_clock_time() >> ((i & 3u) * 8u));
    }
    drbg_update(seed, (uint16_t)sizeof seed);
    for (i = 0; i < sizeof seed; i++) seed[i] = 0u;   /* wipe raw entropy */
    drbg_reseed_ctr = 0u;
}

/**
 * @brief Seed the DRBG on first use and reseed it when the interval is due.
 * @note Call before opening the connection: it is slow the first time and at
 *       each reseed, and no server should be waiting then.
 */
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

/**
 * @brief TLS RNG callback: HMAC-DRBG generate.
 *
 * Never reads the TRNG, so it does not stall a live handshake.
 */
static void basic_https_rng(uint8_t *b, size_t n)
{
    size_t off = 0u;
    if (!drbg_ready) basic_https_rng_prepare();   /* seeds on a first call */
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
/** @brief No TRNG on this build: nothing to seed. */
static void basic_https_rng_prepare(void) { }

/**
 * @brief TLS RNG for a build without a TRNG: clock-derived and weak, for
 *        development builds only.
 */
static void
basic_https_rng(uint8_t *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) b[i] = (uint8_t)(tiku_clock_time() >> (i & 7));
}
#endif

/**
 * @brief Handshake-step hook: kick the watchdog, so a slow handshake survives
 *        while a hang still trips the WDT.
 */
static void basic_tls13_dbg(const char *m)
{
    (void)m;
    tiku_watchdog_kick();
}

/**
 * @brief One pump step while a fetch waits: deliver packets, run TCP timers.
 *
 * Inbound packets are delivered on every call; tcp_periodic is paced to about
 * 8 Hz, because each call advances the connect and retransmit timeouts and a
 * tight loop would expire them early (the same hazard as dns_poll).
 */
static void
basic_https_pump(void)
{
    static tiku_clock_time_t last_tcp;
    tiku_clock_time_t now = tiku_clock_time();
    tiku_watchdog_kick();
    /* WiFi: radio RX arrives through tiku_wireless_rx_poll(), which also lends
     * the CPU to a radio whose task is a worker; a stray keystroke is dropped.
     * SLIP: the console line is the IP transport and the shell loop is blocked
     * in this builtin, so the console decoder is pumped here.  A frame
     * trickles in over many calls and the decoder keeps its state between
     * them; tiku_shell_io_getc() would steal the SLIP bytes. */
#if (TIKU_DRV_WIFI_CYW43_ENABLE + 0) || (TIKU_DRV_WIFI_ESP_ENABLE + 0)
    (void)tiku_wireless_rx_poll();
    if (tiku_shell_io_rx_ready()) (void)tiku_shell_io_getc();
#elif TIKU_SHELL_CMD_SLIP
    tiku_shell_net_pump();          /* console decoder: SLIP -> ipv4_input */
#endif
    if ((tiku_clock_time_t)(now - last_tcp) >= (tiku_clock_time_t)(TIKU_CLOCK_SECOND / 8)) {
        last_tcp = now;
        tiku_kits_net_tcp_periodic();
    }
}

/** Last TCP event on the fetch's connection (TIKU_KITS_NET_TCP_EVT_*). */
static volatile uint8_t basic_https_evt;
/** @brief TCP event callback: record the event for the wait loops. */
static void basic_https_on_evt(tiku_kits_net_tcp_conn_t *c, uint8_t e){ (void)c; basic_https_evt = e; }
/** @brief TCP receive callback: unused, the reads poll the connection. */
static void basic_https_on_rx (tiku_kits_net_tcp_conn_t *c, uint16_t a){ (void)c; (void)a; }

/* Each blocking wait in a fetch gives up 20 s after it starts. */
#define BASIC_HTTPS_DEADLINE() \
    ((tiku_clock_time_t)(tiku_clock_time() + 20u * TIKU_CLOCK_SECOND))
#define BASIC_HTTPS_EXPIRED(dl)  (!TIKU_CLOCK_LT(tiku_clock_time(), (dl)))

/**
 * @brief TLS send callback: transmit @p n bytes in MSS-sized segments.
 * @return @p n, or -1 when the deadline passes first.
 */
static int
basic_https_send(void *ctx, const uint8_t *b, size_t n)
{
    tiku_kits_net_tcp_conn_t *c = ctx;
    size_t off = 0;
    tiku_clock_time_t dl = BASIC_HTTPS_DEADLINE();
    while (off < n) {
        /* tcp_send transmits one segment and rejects data_len > snd_mss, so
         * cut by the negotiated MSS, which is small over SLIP: a fixed larger
         * chunk would be refused on every try until the deadline. */
        uint16_t mss = c->snd_mss ? c->snd_mss : TIKU_KITS_NET_TCP_MSS;
        size_t chunk = n - off; if (chunk > mss) chunk = mss;
        if (tiku_kits_net_tcp_send(c, b + off, (uint16_t)chunk) == TIKU_KITS_NET_OK)
            off += chunk;
        basic_https_pump();
        if (BASIC_HTTPS_EXPIRED(dl)) return -1;
    }
    return (int)n;
}

/**
 * @brief TLS receive callback: read up to @p n bytes, pumping until some come.
 * @return Bytes read, or -1 on a reset, a close with nothing left, or the
 *         deadline.
 */
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

/**
 * @brief Open a TCP connection to @p ip port 443 and pump until it connects.
 *
 * Serves the first attempt and the TLS 1.2 retry.
 *
 * @return The connection, or NULL if it fails or the deadline passes.
 */
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

/** @brief Label for a tiku_kits_crypto_tls13_last_stage code. */
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

/*
 * Heavy-crypto offload onto a worker thread.
 *
 * With worker threads available, io.offload runs the handshake's public-key
 * operations (ECDHE, CertVerify, chain verify) on one dedicated worker while
 * the drive loop pumps the net and dispatches the other processes.  With
 * threads off, or TIKU_BASIC_HTTPS_OFFLOAD 0, io.offload is NULL and they run
 * inline on the shell thread: nothing pumps the net meanwhile, so the peer
 * can reset the connection, and no kernel timer or rule runs.
 */
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
static volatile uint8_t  basic_crypto_busy;   /* crypto in flight */

/** @brief Worker entry: run the queued crypto closure and keep its result. */
static void basic_crypto_worker_body(void *arg)
{
    (void)arg;
    basic_crypto_rc = basic_crypto_fn(basic_crypto_arg);
}

/**
 * @brief io.offload: run @p fn on the crypto worker while the kernel runs.
 *
 * Like the scheduler's idle branch, the loop pumps the net, dispatches every
 * ready process except this one (so a queued shell event cannot re-enter this
 * command) and yields the CPU to the worker until an event wakes it.
 *
 * @note @p fn is a crypto closure over connect()'s stack.  If the worker cannot
 *       start, @p fn runs inline.  tiku_current_process, which call_process()
 *       clears as it fans out, is restored on return.
 * @note At the deadline the step fails, but the worker, which cannot be
 *       cancelled, may still be running @p fn over that stack.
 */
static int basic_https_offload(int (*fn)(void *), void *arg)
{
    struct tiku_process *owner = tiku_current_process;
    tiku_clock_time_t dl;

    if (basic_crypto_busy) {         /* non-reentrant primitives */
        return fn(arg);
    }
    basic_crypto_fn  = fn;
    basic_crypto_arg = arg;
    basic_crypto_rc  = -1;           /* failure until the worker stores fn's */
    basic_crypto_busy = 1;

    if (tiku_thread_start(&basic_crypto_worker,
                          basic_crypto_worker_body, 0) != 0) {
        basic_crypto_busy = 0;
        return fn(arg);              /* worker unavailable: inline */
    }

    dl = BASIC_HTTPS_DEADLINE();
    while (basic_crypto_worker.state != TIKU_THREAD_DONE) {
        basic_https_pump();                          /* net + WDT stay alive */
        while (tiku_process_run_except(owner)) { }   /* others' timers/rules */
        tiku_atomic_enter();
        if (!tiku_process_queue_dispatchable_except(owner) &&
            tiku_thread_worker_ready()) {
            tiku_thread_kernel_block();               /* CPU -> the crypto */
        }
        tiku_atomic_exit();
        if (BASIC_HTTPS_EXPIRED(dl)) {                /* give up at deadline */
            break;
        }
    }

    tiku_current_process = owner;    /* the drain cleared it */
    basic_crypto_busy = 0;
    return basic_crypto_rc;
}
#endif /* TIKU_BASIC_HTTPS_OFFLOAD */

/*
 * Adapters that let basic_https_get() drive the shared kit HTTPS engine
 * (tiku_kits_net_http_cert_exchange) over BASIC's own transport: a reconnect
 * for the TLS 1.2 fallback and a sink that collects the response.
 */

/** Reconnect context: the server address and the first source port. */
struct basic_https_rc_ctx { const uint8_t *ip; uint16_t src; };
/**
 * @brief Reconnect for the TLS 1.2 fallback: close @p old and open a fresh
 *        4-tuple (source port + 1).
 */
static void *
basic_https_reconnect(void *c, void *old)
{
    struct basic_https_rc_ctx *x = c;
    tiku_kits_net_tcp_close((tiku_kits_net_tcp_conn_t *)old);
    return basic_https_open(x->ip, (uint16_t)(x->src + 1));
}

/** Sink context: the output buffer and the bytes stored so far. */
struct basic_https_sink_ctx { char *out; size_t cap; size_t total; };
/**
 * @brief Response sink: append decrypted bytes to out[] up to cap - 1.
 * @return 1 to keep reading, 0 once out[] is full.
 */
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

/**
 * @brief HTTPS @p method request for @p host @p path, raw response in @p out.
 *
 * Resolves, connects and runs the kit's certificate TLS, pumping throughout.
 * Every request carries the HTTPHEADER lines; a non-NULL @p body is sent with
 * Content-Type @p ctype (default JSON).  Sets basic_http_status.
 *
 * @return The response length (status line, headers and body, NUL-terminated
 *         in @p out, at most @p cap - 1), or -1 on any failure.
 */
static int
basic_https_get(const char *method, const char *host, const char *path,
                const char *body, const char *ctype, char *out, size_t cap)
{
    tiku_kits_crypto_tls13_io_t io;
    tiku_kits_net_tcp_conn_t   *tcp;
    uint8_t  ip[4];
    char     req[TIKU_BASIC_HTTP_REQ_MAX];  /* request head, asserted above */
    size_t   total = 0, rl;
    tiku_clock_time_t dl;
    const tiku_kits_crypto_x509_root_t *roots = NULL;
    int      nroots = 0;
    static uint16_t src_seq = 49150;  /* fresh ephemeral port pair per call */

    basic_http_status = 0;

    /* The trust store is checked before DNS and TCP: the roots live in /data
     * and can be missing, and a missing store is reported by name, with the
     * provisioning hint, before any network traffic. */
    if (basic_https_roots_get(&roots, &nroots) != 0) {
        basic_report(TIKU_BASIC_ERR_IO,
                     "HTTPGET: no trust store -- provision /data/"
                     BASIC_HTTPS_ROOTS_FILE " (tools/gen_roots.py)");
        return -1;
    }
#if TIKU_BASIC_HTTPS_OFFLOAD
    /* Refuse a fetch triggered from a process the crypto drive loop dispatched
     * (a rule/timer that fetches while another fetch's crypto is in flight):
     * the crypto primitives are non-reentrant, so a nested handshake would
     * corrupt the worker's in-flight state.  Serial fetches only. */
    if (basic_crypto_busy) {
        basic_report(TIKU_BASIC_ERR_NET, "HTTPGET: busy (crypto in flight)");
        return -1;
    }
#endif
    /* A new source port on every call gives a redirect refetch to the same
     * server IP a fresh 4-tuple: the closed connection's 4-tuple can still be
     * in TIME_WAIT, and a SYN that reuses it is dropped. */
    src_seq = (src_seq >= 60000u) ? 49152u : (uint16_t)(src_seq + 2);

    /* Set up the TCP table the connect below allocates from; the call is
     * idempotent.  A lean WiFi build has no TIKU_SHELL_NET_TEST setup to make
     * it first. */
    tiku_kits_net_tcp_init();

    /* resolve host (literal dotted-quad accepted directly) */
    if (basic_net_parse_ip(host, ip) != 0) {
        uint8_t dnssrv[4];                 /* DHCP lease dns, else 8.8.8.8 */
        int8_t drc;
        tiku_kits_net_dns_default_server(dnssrv);
        tiku_kits_net_dns_init();
        tiku_kits_net_dns_set_server(dnssrv);
        tiku_clock_time_t np;
        drc = tiku_kits_net_dns_resolve(host);
        if (drc != TIKU_KITS_NET_OK) {
            basic_report(TIKU_BASIC_ERR_NET, "HTTPGET: DNS error"); return -1; }
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

    /* Seed the TLS RNG (HMAC-DRBG) before opening the connection: the TRNG
     * gather is slow, and here no peer is waiting on the handshake. */
    basic_https_rng_prepare();

    /* TCP connect :443 */
    tcp = basic_https_open(ip, src_seq);
    if (tcp == NULL) { basic_report(TIKU_BASIC_ERR_NET, "HTTPGET: TCP connect failed"); return -1; }
    (void)dl;

    /* Build the request up front (independent of the negotiated TLS version):
     * <METHOD> <path> HTTP/1.0 + Host + any HTTPHEADER lines, and -- for a body
     * (POST) -- Content-Type + Content-Length.  The body follows as a second
     * TLS record so it need not fit in req[]. */
    req[0] = '\0';
    strcat(req, method); strcat(req, " "); strcat(req, path);
    strcat(req, " HTTP/1.0\r\nHost: "); strcat(req, host);
    strcat(req, "\r\nConnection: close\r\n");
    if (basic_http_hdrs[0]) strcat(req, basic_http_hdrs);  /* lines end \r\n */
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
    strcat(req, "\r\n");                                   /* end of headers */
    rl = strlen(req);

    /* Hand the connected socket to the shared kit engine: it runs the TLS 1.3
     * cert handshake over this transport (basic_https_send/recv), falls back to
     * TLS 1.2 on a fresh connection via basic_https_reconnect (the ServerHello
     * is consumed on a 1.3 failure), sends the request + body, and streams the
     * response into out[] through basic_https_sink.
     *
     * now_unix gates cert validity: the wall-clock time once SETTIME or NTP
     * has set the RTC (tiku_rtc_is_set()), else 0, which skips the date
     * window -- signature, trust anchor and hostname stay enforced.
     *
     * The WDT is not paused: basic_tls13_dbg kicks it per handshake step, so a
     * slow handshake survives while a hang still trips the WDT. */
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

        /* Map the roots again: a re-provision since the check at entry
         * invalidates the earlier pointers (a tiku_tfs_map() pointer dies on
         * the next write to that name). */
        if (basic_https_roots_get(&roots, &nroots) != 0) {
            basic_report(TIKU_BASIC_ERR_IO, "HTTPGET: trust store vanished");
            return -1;
        }
        tconf.trust    = TIKU_KITS_NET_HTTP_CERT;
        tconf.roots    = roots;
        tconf.nroots   = nroots;
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
                basic_report(TIKU_BASIC_ERR_NET, "HTTPGET: TCP connect failed");
            } else {
                int      st = tiku_kits_crypto_tls13_last_stage;
                uint32_t rx = tiku_kits_crypto_tls13_last_rx;
                int      ev = basic_https_evt;   /* RST? closed? silent? */
                /* The request is spent, so req[] holds the message. */
                snprintf(req, sizeof req,
                    "HTTPGET: TLS failed -- tls1.3 stage %d (%s), %u B in, "
                    "link=%s",
                    st, basic_tls_stage_str(st), (unsigned)rx,
                    (ev == TIKU_KITS_NET_TCP_EVT_ABORTED ? "RST" :
                     ev == TIKU_KITS_NET_TCP_EVT_CLOSED  ? "closed" :
                     "silent"));
                basic_report(TIKU_BASIC_ERR_NET, req);
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
