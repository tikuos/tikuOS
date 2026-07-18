/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_smp.c - LE Secure Connections crypto (AES-CMAC + f4/f5/f6) and a
 * self-test.  Phase E foundation; the pairing state machine builds on this.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/bluetooth/tiku_ble_smp.h>
#include <arch/nordic/tiku_crypto_arch.h>          /* AES-128 ECB (CRACEN)    */
#include <arch/nordic/tiku_trng_arch.h>            /* seed for the keypair    */
#include <tikukits/crypto/p256/tiku_kits_crypto_p256.h>
#include <string.h>

/* --- AES-CMAC (RFC 4493) over AES-128 ECB ------------------------------- */

static int aes_ecb(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    return tiku_crypto_arch_aes_ecb(0, key, 16u, in, out);
}

/* one-bit left shift of a 16-byte big-endian value */
static void cmac_lshift(const uint8_t in[16], uint8_t out[16])
{
    int i;
    uint8_t carry = 0u;
    for (i = 15; i >= 0; i--) {
        out[i] = (uint8_t)((in[i] << 1) | carry);
        carry = (uint8_t)((in[i] >> 7) & 1u);
    }
}

static int cmac_subkeys(const uint8_t key[16], uint8_t k1[16], uint8_t k2[16])
{
    uint8_t l[16];
    uint8_t zero[16];
    memset(zero, 0, 16);
    if (aes_ecb(key, zero, l) != 0) {
        return -1;
    }
    cmac_lshift(l, k1);
    if ((l[0] & 0x80u) != 0u) {
        k1[15] ^= 0x87u;
    }
    cmac_lshift(k1, k2);
    if ((k1[0] & 0x80u) != 0u) {
        k2[15] ^= 0x87u;
    }
    return 0;
}

int tiku_ble_smp_aes_cmac(const uint8_t key[16], const uint8_t *msg,
                          size_t len, uint8_t mac[16])
{
    uint8_t  k1[16], k2[16], x[16], y[16], block[16];
    size_t   n, i, j, rem;
    int      last_complete;

    if (cmac_subkeys(key, k1, k2) != 0) {
        return -1;
    }
    n = (len + 15u) / 16u;
    if (n == 0u) {
        n = 1u; last_complete = 0;
    } else {
        last_complete = ((len % 16u) == 0u) ? 1 : 0;
    }
    memset(x, 0, 16);
    for (i = 0u; i + 1u < n; i++) {                /* all but the last block  */
        for (j = 0u; j < 16u; j++) {
            y[j] = (uint8_t)(x[j] ^ msg[16u*i + j]);
        }
        if (aes_ecb(key, y, x) != 0) {
            return -1;
        }
    }
    if (last_complete) {                            /* M_last ^ K1             */
        for (j = 0u; j < 16u; j++) {
            block[j] = (uint8_t)(msg[16u*(n-1u) + j] ^ k1[j]);
        }
    } else {                                        /* pad(M_last) ^ K2        */
        rem = len - 16u*(n-1u);
        for (j = 0u; j < 16u; j++) {
            uint8_t b = (j < rem) ? msg[16u*(n-1u) + j]
                                  : ((j == rem) ? 0x80u : 0x00u);
            block[j] = (uint8_t)(b ^ k2[j]);
        }
    }
    for (j = 0u; j < 16u; j++) {
        y[j] = (uint8_t)(x[j] ^ block[j]);
    }
    return aes_ecb(key, y, mac);
}

/* --- SMP f4 / f5 / f6 (Core Spec Vol 3, Part H, 2.2) -------------------- */

void tiku_ble_smp_f4(const uint8_t u[32], const uint8_t v[32],
                     const uint8_t x[16], uint8_t z, uint8_t out[16])
{
    uint8_t m[65];
    memcpy(&m[0], u, 32);
    memcpy(&m[32], v, 32);
    m[64] = z;
    (void)tiku_ble_smp_aes_cmac(x, m, 65u, out);
}

void tiku_ble_smp_f5(const uint8_t w[32], const uint8_t n1[16],
                     const uint8_t n2[16], const uint8_t a1[7],
                     const uint8_t a2[7], uint8_t mackey[16], uint8_t ltk[16])
{
    static const uint8_t salt[16] = {
        0x6Cu, 0x88u, 0x83u, 0x91u, 0xAAu, 0xF5u, 0xA5u, 0x38u,
        0x60u, 0x37u, 0x0Bu, 0xDBu, 0x5Au, 0x60u, 0x83u, 0xBEu };
    uint8_t t[16], m[53];

    (void)tiku_ble_smp_aes_cmac(salt, w, 32u, t);  /* T = CMAC_SALT(W)        */
    /* m = Counter || keyID("btle") || N1 || N2 || A1 || A2 || Length(256) */
    m[1] = 0x62u; m[2] = 0x74u; m[3] = 0x6Cu; m[4] = 0x65u;   /* keyID       */
    memcpy(&m[5], n1, 16);
    memcpy(&m[21], n2, 16);
    memcpy(&m[37], a1, 7);
    memcpy(&m[44], a2, 7);
    m[51] = 0x01u; m[52] = 0x00u;                  /* Length = 256            */
    m[0] = 0x00u;
    (void)tiku_ble_smp_aes_cmac(t, m, 53u, mackey);
    m[0] = 0x01u;
    (void)tiku_ble_smp_aes_cmac(t, m, 53u, ltk);
}

void tiku_ble_smp_f6(const uint8_t w[16], const uint8_t n1[16],
                     const uint8_t n2[16], const uint8_t r[16],
                     const uint8_t iocap[3], const uint8_t a1[7],
                     const uint8_t a2[7], uint8_t out[16])
{
    uint8_t m[65];
    memcpy(&m[0], n1, 16);
    memcpy(&m[16], n2, 16);
    memcpy(&m[32], r, 16);
    memcpy(&m[48], iocap, 3);
    memcpy(&m[51], a1, 7);
    memcpy(&m[58], a2, 7);
    (void)tiku_ble_smp_aes_cmac(w, m, 65u, out);
}

/* --- self-test --------------------------------------------------------- */

int tiku_ble_smp_selftest(void)
{
    /* RFC 4493 CMAC KAT: K = 2b7e...4f3c, empty message. */
    static const uint8_t k[16] = {
        0x2Bu,0x7Eu,0x15u,0x16u,0x28u,0xAEu,0xD2u,0xA6u,
        0xABu,0xF7u,0x15u,0x88u,0x09u,0xCFu,0x4Fu,0x3Cu };
    static const uint8_t want[16] = {
        0xBBu,0x1Du,0x69u,0x29u,0xE9u,0x59u,0x37u,0x28u,
        0x7Fu,0xA3u,0x7Du,0x12u,0x9Bu,0x75u,0x67u,0x46u };
    uint8_t mac[16];
    uint8_t seed_a[32], seed_b[32], priv_a[32], priv_b[32];
    uint8_t pub_a[65], pub_b[65], sh_a[32], sh_b[32];  /* 0x04||X||Y = 65 B  */
    int result = 0;

    if (tiku_ble_smp_aes_cmac(k, (const uint8_t *)0, 0u, mac) == 0 &&
        memcmp(mac, want, 16) == 0) {
        result |= 1;                               /* bit0: CMAC KAT          */
    }

    tiku_trng_arch_init();
    if (tiku_trng_arch_read_bytes(seed_a, 32) == 0 &&
        tiku_trng_arch_read_bytes(seed_b, 32) == 0 &&
        tiku_kits_crypto_p256_ecdh_keypair(seed_a, priv_a, pub_a) == 0 &&
        tiku_kits_crypto_p256_ecdh_keypair(seed_b, priv_b, pub_b) == 0 &&
        tiku_kits_crypto_p256_ecdh_shared(priv_a, pub_b, sh_a) == 0 &&
        tiku_kits_crypto_p256_ecdh_shared(priv_b, pub_a, sh_b) == 0 &&
        memcmp(sh_a, sh_b, 32) == 0) {
        result |= 2;                               /* bit1: ECDH round-trip   */
    }
    return result;
}
