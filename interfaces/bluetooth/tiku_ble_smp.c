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

/*
 * SMP fields travel the wire little-endian, but the CMAC core (and the spec's
 * f4/f5/f6 definitions) operate big-endian.  Each function reverses its inputs
 * into a big-endian scratch, CMACs, then reverses the result back.  swap()
 * copies src->dst reversed; swap_ip() reverses in place.
 */
static void swap(uint8_t *dst, const uint8_t *src, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        dst[i] = src[n - 1u - i];
    }
}

static void swap_ip(uint8_t *b, size_t n)
{
    size_t i;
    for (i = 0u; i < n / 2u; i++) {
        uint8_t t = b[i];
        b[i] = b[n - 1u - i];
        b[n - 1u - i] = t;
    }
}

void tiku_ble_smp_f4(const uint8_t u[32], const uint8_t v[32],
                     const uint8_t x[16], uint8_t z, uint8_t out[16])
{
    uint8_t m[65], xs[16];
    swap(&m[0], u, 32);
    swap(&m[32], v, 32);
    m[64] = z;
    swap(xs, x, 16);
    (void)tiku_ble_smp_aes_cmac(xs, m, 65u, out);
    swap_ip(out, 16);
}

void tiku_ble_smp_f5(const uint8_t w[32], const uint8_t n1[16],
                     const uint8_t n2[16], uint8_t a1t, const uint8_t a1[6],
                     uint8_t a2t, const uint8_t a2[6],
                     uint8_t mackey[16], uint8_t ltk[16])
{
    static const uint8_t salt[16] = {
        0x6Cu, 0x88u, 0x83u, 0x91u, 0xAAu, 0xF5u, 0xA5u, 0x38u,
        0x60u, 0x37u, 0x0Bu, 0xDBu, 0x5Au, 0x60u, 0x83u, 0xBEu };
    uint8_t t[16], ws[32], m[53];

    swap(ws, w, 32);
    (void)tiku_ble_smp_aes_cmac(salt, ws, 32u, t);  /* T = CMAC_SALT(W)       */
    /* m = Counter || keyID("btle") || N1 || N2 || A1 || A2 || Length(256) */
    m[1] = 0x62u; m[2] = 0x74u; m[3] = 0x6Cu; m[4] = 0x65u;   /* keyID       */
    swap(&m[5], n1, 16);
    swap(&m[21], n2, 16);
    m[37] = a1t; swap(&m[38], a1, 6);
    m[44] = a2t; swap(&m[45], a2, 6);
    m[51] = 0x01u; m[52] = 0x00u;                  /* Length = 256            */
    m[0] = 0x00u;
    (void)tiku_ble_smp_aes_cmac(t, m, 53u, mackey);
    swap_ip(mackey, 16);
    m[0] = 0x01u;
    (void)tiku_ble_smp_aes_cmac(t, m, 53u, ltk);
    swap_ip(ltk, 16);
}

void tiku_ble_smp_f6(const uint8_t w[16], const uint8_t n1[16],
                     const uint8_t n2[16], const uint8_t r[16],
                     const uint8_t iocap[3], uint8_t a1t, const uint8_t a1[6],
                     uint8_t a2t, const uint8_t a2[6], uint8_t out[16])
{
    uint8_t m[65], ws[16];
    swap(&m[0], n1, 16);
    swap(&m[16], n2, 16);
    swap(&m[32], r, 16);
    swap(&m[48], iocap, 3);
    m[51] = a1t; swap(&m[52], a1, 6);
    m[58] = a2t; swap(&m[59], a2, 6);
    swap(ws, w, 16);
    (void)tiku_ble_smp_aes_cmac(ws, m, 65u, out);
    swap_ip(out, 16);
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

    /* f4/f5/f6 known-answer vectors (Core Spec Vol 3, Part H, D.2-D.4). */
    {
        static const uint8_t u[32] = {
            0xe6,0x9d,0x35,0x0e,0x48,0x01,0x03,0xcc,0xdb,0xfd,0xf4,0xac,
            0x11,0x91,0xf4,0xef,0xb9,0xa5,0xf9,0xe9,0xa7,0x83,0x2c,0x5e,
            0x2c,0xbe,0x97,0xf2,0xd2,0x03,0xb0,0x20 };
        static const uint8_t v[32] = {
            0xfd,0xc5,0x7f,0xf4,0x49,0xdd,0x4f,0x6b,0xfb,0x7c,0x9d,0xf1,
            0xc2,0x9a,0xcb,0x59,0x2a,0xe7,0xd4,0xee,0xfb,0xfc,0x0a,0x90,
            0x9a,0xbb,0xf6,0x32,0x3d,0x8b,0x18,0x55 };
        static const uint8_t x[16] = {
            0xab,0xae,0x2b,0x71,0xec,0xb2,0xff,0xff,
            0x3e,0x73,0x77,0xd1,0x54,0x84,0xcb,0xd5 };
        static const uint8_t f4_exp[16] = {
            0x2d,0x87,0x74,0xa9,0xbe,0xa1,0xed,0xf1,
            0x1c,0xbd,0xa9,0x07,0xf1,0x16,0xc9,0xf2 };
        static const uint8_t w5[32] = {
            0x98,0xa6,0xbf,0x73,0xf3,0x34,0x8d,0x86,0xf1,0x66,0xf8,0xb4,
            0x13,0x6b,0x79,0x99,0x9b,0x7d,0x39,0x0a,0xa6,0x10,0x10,0x34,
            0x05,0xad,0xc8,0x57,0xa3,0x34,0x02,0xec };
        static const uint8_t n1[16] = {
            0xab,0xae,0x2b,0x71,0xec,0xb2,0xff,0xff,
            0x3e,0x73,0x77,0xd1,0x54,0x84,0xcb,0xd5 };
        static const uint8_t n2[16] = {
            0xcf,0xc4,0x3d,0xff,0xf7,0x83,0x65,0x21,
            0x6e,0x5f,0xa7,0x25,0xcc,0xe7,0xe8,0xa6 };
        static const uint8_t a1[6] = { 0xce,0xbf,0x37,0x37,0x12,0x56 };
        static const uint8_t a2[6] = { 0xc1,0xcf,0x2d,0x70,0x13,0xa7 };
        static const uint8_t mk_exp[16] = {
            0x20,0x6e,0x63,0xce,0x20,0x6a,0x3f,0xfd,
            0x02,0x4a,0x08,0xa1,0x76,0xf1,0x65,0x29 };
        static const uint8_t ltk_exp[16] = {
            0x38,0x0a,0x75,0x94,0xb5,0x22,0x05,0x98,
            0x23,0xcd,0xd7,0x69,0x11,0x79,0x86,0x69 };
        static const uint8_t r6[16] = {
            0xc8,0x0f,0x2d,0x0c,0xd2,0x42,0xda,0x08,
            0x54,0xbb,0x53,0xb4,0x3b,0x34,0xa3,0x12 };
        static const uint8_t iocap[3] = { 0x02,0x01,0x01 };
        static const uint8_t f6_exp[16] = {
            0x61,0x8f,0x95,0xda,0x09,0x0b,0x6c,0xd2,
            0xc5,0xe8,0xd0,0x9c,0x98,0x73,0xc4,0xe3 };
        uint8_t of4[16], of6[16], mk[16], ltk[16];

        tiku_ble_smp_f4(u, v, x, 0x00u, of4);
        tiku_ble_smp_f5(w5, n1, n2, 0x00u, a1, 0x00u, a2, mk, ltk);
        tiku_ble_smp_f6(mk_exp, n1, n2, r6, iocap, 0x00u, a1, 0x00u, a2, of6);
        if (memcmp(of4, f4_exp, 16) == 0 &&
            memcmp(mk, mk_exp, 16) == 0 &&
            memcmp(ltk, ltk_exp, 16) == 0 &&
            memcmp(of6, f6_exp, 16) == 0) {
            result |= 4;                           /* bit2: f4/f5/f6 KATs     */
        }
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
