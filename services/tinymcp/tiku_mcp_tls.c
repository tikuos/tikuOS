/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_tls.c - the board's TLS identity in two durable cells, its
 * nodes under /sys/mcp/tls, and the handshakes' random generator.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mcp_tls.h"

#include <stdio.h>
#include <string.h>

#include "tiku.h"
#include "tiku_mcp.h"
#include "tiku_mcp_ask.h"
#include <kernel/memory/tiku_mem.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/vfs/tiku_vfs.h>
#include <tikukits/crypto/base64/tiku_kits_crypto_base64.h>
#include <tikukits/crypto/hmac/tiku_kits_crypto_hmac.h>
#include <tikukits/crypto/p256/tiku_kits_crypto_p256.h>
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>

#if defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_trng_arch.h>
#elif defined(PLATFORM_ESP32C5)
#include <arch/esp32c5/tiku_trng_arch.h>
#elif defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_trng_arch.h>
#elif defined(PLATFORM_ESP32C6)
#include <arch/esp32c6/tiku_trng_arch.h>
#elif defined(PLATFORM_ESP32P4)
#include <arch/esp32p4/tiku_trng_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_trng_arch.h>
#elif defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_trng_arch.h>
#elif defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_trng_arch.h>
#elif defined(PLATFORM_RA8P1)
#include <arch/ra8p1/tiku_trng_arch.h>
#else
#error "TIKU_MCP_TLS_ENABLE needs a TRNG: this port has none"
#endif

#define KEY_MAGIC  0x4D544C4BUL    /* 'MTLK' */
#define CERT_MAGIC 0x4D544C43UL    /* 'MTLC' */

#ifndef TIKU_MCP_TLS_RESEED
#define TIKU_MCP_TLS_RESEED 64u    /* generator outputs between TRNG reseeds */
#endif

/*---------------------------------------------------------------------------*/
/* THE CELLS                                                                 */
/*---------------------------------------------------------------------------*/

/* The private scalar; all zero for none.  No node reads it. */
static TIKU_DURABLE uint8_t key_persist[32];
TIKU_PERSIST_CELL(key_cell, key_persist, KEY_MAGIC, NULL, 0);

/* The certificate's DER; len 0 for none or while one is being written. */
typedef struct {
    uint16_t len;
    uint8_t  der[TIKU_MCP_TLS_CERT_MAX];
} cert_cell_t;
static TIKU_DURABLE cert_cell_t cert_persist;
TIKU_PERSIST_CELL(cert_cell, cert_persist, CERT_MAGIC, NULL, 0);

static uint16_t staged;            /* DER bytes of a certificate being written */
static tiku_kits_crypto_tls_server_cred_t cred;

void
tiku_mcp_tls_init(void)
{
    (void)tiku_persist_cell_init(&key_cell);
    (void)tiku_persist_cell_init(&cert_cell);
}

static int
key_set(void)
{
    uint8_t any = 0u;
    unsigned i;

    for (i = 0; i < sizeof key_persist; i++) {
        any |= key_persist[i];
    }
    return any != 0u;
}

const tiku_kits_crypto_tls_server_cred_t *
tiku_mcp_tls_cred(void)
{
    if (!key_set() || cert_persist.len == 0u ||
        cert_persist.len > TIKU_MCP_TLS_CERT_MAX) {
        return (const tiku_kits_crypto_tls_server_cred_t *)0;
    }
    cred.chain[0].der = cert_persist.der;
    cred.chain[0].len = cert_persist.len;
    cred.nchain = 1u;
    cred.key = key_persist;
    return &cred;
}

/** @brief Removes the certificate, the one being written included. */
static void
cert_clear(void)
{
    static const uint16_t none = 0u;

    staged = 0u;
    tiku_persist_cell_commit(&cert_cell, &none, (uint16_t)sizeof none);
}

/*---------------------------------------------------------------------------*/
/* THE GENERATOR: HMAC-DRBG (SP 800-90A), SEEDED FROM THE TRNG               */
/*---------------------------------------------------------------------------*/

#define SEED_BYTES 48u

static uint8_t  drbg_K[32], drbg_V[32];
static uint8_t  drbg_ready;
static uint32_t drbg_outputs;

/* HMAC-SHA-256 through a temporary, so @p out may alias @p key or @p data. */
static void
drbg_hmac(const uint8_t *key, const uint8_t *data, uint16_t n, uint8_t out[32])
{
    uint8_t t[32];

    (void)tiku_kits_crypto_hmac_sha256(key, 32u, data, n, t);
    memcpy(out, t, 32u);
}

/* The Update function (10.1.2.2) with @p n bytes of provided data. */
static void
drbg_update(const uint8_t *pd, uint16_t n)
{
    uint8_t m[32u + 1u + SEED_BYTES];
    uint8_t sep;

    for (sep = 0u; sep < (n > 0u ? 2u : 1u); sep++) {
        memcpy(m, drbg_V, 32u);
        m[32] = sep;
        if (n > 0u) {
            memcpy(m + 33, pd, n);
        }
        drbg_hmac(drbg_K, m, (uint16_t)(33u + n), drbg_K);
        drbg_hmac(drbg_K, drbg_V, 32u, drbg_V);
    }
    memset(m, 0, sizeof m);
}

void
tiku_mcp_tls_seed(void)
{
    uint8_t seed[SEED_BYTES];
    unsigned i;

    if (drbg_ready && drbg_outputs < TIKU_MCP_TLS_RESEED) {
        return;
    }
    if (!drbg_ready) {
        memset(drbg_K, 0x00, sizeof drbg_K);
        memset(drbg_V, 0x01, sizeof drbg_V);
        drbg_ready = 1u;
    }
    if (tiku_trng_arch_read_bytes(seed, sizeof seed) != TIKU_TRNG_OK) {
        /* The state still moves; the next seed call tries the TRNG again. */
        for (i = 0; i < sizeof seed; i++) {
            seed[i] ^= (uint8_t)(tiku_clock_time() >> ((i & 3u) * 8u));
        }
        drbg_outputs = TIKU_MCP_TLS_RESEED;
    } else {
        drbg_outputs = 0u;
    }
    drbg_update(seed, (uint16_t)sizeof seed);
    memset(seed, 0, sizeof seed);
}

void
tiku_mcp_tls_rng(uint8_t *buf, size_t len)
{
    size_t off = 0u;

    if (!drbg_ready) {
        tiku_mcp_tls_seed();
    }
    while (off < len) {
        size_t take = len - off < 32u ? len - off : 32u;

        drbg_hmac(drbg_K, drbg_V, 32u, drbg_V);
        memcpy(buf + off, drbg_V, take);
        off += take;
    }
    drbg_update((const uint8_t *)0, 0u);
    drbg_outputs++;
}

/*---------------------------------------------------------------------------*/
/* THE NODES                                                                 */
/*---------------------------------------------------------------------------*/

int
tiku_mcp_tls_key_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", key_set() ? "set" : "none");
}

int
tiku_mcp_tls_key_write(const char *buf, size_t len)
{
    uint8_t seed[32], priv[32], pub[65];
    int made = 0;

    if (len > 0u && buf[len - 1u] == '\n') {
        len--;
    }
    if (len == 4u && memcmp(buf, "none", 4u) == 0) {
        memset(priv, 0, sizeof priv);
    } else if (len == 3u && memcmp(buf, "new", 3u) == 0) {
        do {
            if (tiku_trng_arch_read_bytes(seed, sizeof seed) != TIKU_TRNG_OK) {
                memset(seed, 0, sizeof seed);
                return TIKU_VFS_EIO;
            }
        } while (tiku_kits_crypto_p256_ecdh_keypair(seed, priv, pub) != 0);
        made = 1;
    } else {
        return TIKU_VFS_EINVAL;
    }
    tiku_persist_cell_commit(&key_cell, priv, (uint16_t)sizeof priv);
    cert_clear();
    memset(seed, 0, sizeof seed);
    memset(priv, 0, sizeof priv);
    tiku_mcp_journal(tiku_mcp_actor(),
                     made ? "made a TLS key" : "removed the TLS key");
    return 0;
}

/* The public point of the board's key into @p pub; -1 for none. */
static int
public_point(uint8_t pub[65])
{
    return key_set() && tiku_kits_crypto_p256_public(key_persist, pub) == 0
               ? 0 : -1;
}

int
tiku_mcp_tls_pub_read(char *buf, size_t max)
{
    uint8_t pub[65];
    size_t i, at = 0u;

    if (public_point(pub) != 0) {
        return snprintf(buf, max, "none\n");
    }
    if (max < 2u * sizeof pub + 2u) {
        return TIKU_VFS_EINVAL;
    }
    for (i = 0; i < sizeof pub; i++) {
        at += (size_t)snprintf(buf + at, max - at, "%02x", (unsigned)pub[i]);
    }
    buf[at++] = '\n';
    buf[at] = 0;
    return (int)at;
}

int
tiku_mcp_tls_cert_read(char *buf, size_t max)
{
    uint8_t h[32];
    size_t i;
    int at;

    if (staged > 0u) {
        return snprintf(buf, max, "staging, %u bytes\n", (unsigned)staged);
    }
    if (cert_persist.len == 0u || cert_persist.len > TIKU_MCP_TLS_CERT_MAX) {
        return snprintf(buf, max, "none\n");
    }
    tiku_kits_crypto_sha256_hash(cert_persist.der, cert_persist.len, h);
    at = snprintf(buf, max, "%u bytes, sha256 ", (unsigned)cert_persist.len);
    for (i = 0; i < sizeof h && at > 0 && (size_t)at + 3u < max; i++) {
        at += snprintf(buf + at, max - (size_t)at, "%02x", (unsigned)h[i]);
    }
    if (at > 0 && (size_t)at + 1u < max) {
        buf[at++] = '\n';
        buf[at] = 0;
    }
    return at;
}

/* The DER's whole length from its SEQUENCE header, 0 until known. */
static uint16_t
der_total(const uint8_t *d, uint16_t have)
{
    if (have < 4u || d[0] != 0x30u) {
        return 0u;
    }
    if (d[1] == 0x82u) {
        return (uint16_t)(4u + ((uint16_t)d[2] << 8) + d[3]);
    }
    if (d[1] == 0x81u) {
        return (uint16_t)(3u + d[2]);
    }
    return (uint16_t)(2u + d[1]);
}

/* 1 when the certificate's subject key is the board's: the BIT STRING
 * 03 42 00 04 X Y holding its public point appears in the DER. */
static int
names_our_key(const uint8_t *d, uint16_t n)
{
    uint8_t want[68];
    uint16_t i;

    want[0] = 0x03u;
    want[1] = 0x42u;
    want[2] = 0x00u;
    if (public_point(want + 3) != 0) {
        return 0;
    }
    for (i = 0; i + sizeof want <= n; i++) {
        if (memcmp(d + i, want, sizeof want) == 0) {
            return 1;
        }
    }
    return 0;
}

int
tiku_mcp_tls_cert_write(const char *buf, size_t len)
{
    uint16_t total, saved;

    if (len > 0u && buf[len - 1u] == '\n') {
        len--;
    }
    if (len == 4u && memcmp(buf, "none", 4u) == 0) {
        cert_clear();
        tiku_mcp_journal(tiku_mcp_actor(), "removed the TLS certificate");
        return 0;
    }
    if (!key_set() || len == 0u || (len % 4u) != 0u) {
        return TIKU_VFS_EINVAL;
    }
    if (staged == 0u && cert_persist.len != 0u) {
        saved = tiku_mpu_unlock_nvm();
        cert_persist.len = 0u;
        tiku_mpu_lock_nvm(saved);
    }
    while (len > 0u) {
        uint8_t part[48];
        uint16_t got = 0u, take = (uint16_t)(len < 64u ? len : 64u);

        if (tiku_kits_crypto_base64_decode(buf, take, part, sizeof part, &got) != 0 ||
            (uint32_t)staged + got > TIKU_MCP_TLS_CERT_MAX) {
            cert_clear();
            return TIKU_VFS_EINVAL;
        }
        saved = tiku_mpu_unlock_nvm();
        memcpy(cert_persist.der + staged, part, got);
        tiku_mpu_lock_nvm(saved);
        staged = (uint16_t)(staged + got);
        buf += take;
        len -= take;
    }
    if (staged < 4u) {
        return 0;
    }
    total = der_total(cert_persist.der, staged);
    if (total == 0u || total > TIKU_MCP_TLS_CERT_MAX) {
        cert_clear();
        return TIKU_VFS_EINVAL;
    }
    if (staged < total) {
        return 0;
    }
    if (staged > total || !names_our_key(cert_persist.der, total)) {
        cert_clear();
        return TIKU_VFS_EINVAL;
    }
    staged = 0u;
    tiku_persist_cell_commit(&cert_cell, &total, (uint16_t)sizeof total);
    tiku_mcp_journal(tiku_mcp_actor(), "installed a TLS certificate");
    return 0;
}
