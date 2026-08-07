/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_sha256.c - SHA-256, freestanding, compiled into BOTH cores.
 *
 * One source for the M85 baseline and the M33 payload, so an A/B between
 * them compares silicon rather than implementations.  No libc, no statics
 * beyond the round constants: the payload has no .data/.bss loader.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu1_sha256.h"

static const uint32_t sha_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

#define ROR(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))

/** @brief One compression round over a prepared 64-byte block. */
static void sha_block(uint32_t st[8], const uint8_t blk[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t i;

    for (i = 0U; i < 16U; i++) {
        w[i] = ((uint32_t)blk[i * 4U] << 24) |
               ((uint32_t)blk[i * 4U + 1U] << 16) |
               ((uint32_t)blk[i * 4U + 2U] << 8) |
               (uint32_t)blk[i * 4U + 3U];
    }
    for (i = 16U; i < 64U; i++) {
        uint32_t s0 = ROR(w[i - 15U], 7) ^ ROR(w[i - 15U], 18) ^
                      (w[i - 15U] >> 3);
        uint32_t s1 = ROR(w[i - 2U], 17) ^ ROR(w[i - 2U], 19) ^
                      (w[i - 2U] >> 10);

        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = st[0]; b = st[1]; c = st[2]; d = st[3];
    e = st[4]; f = st[5]; g = st[6]; h = st[7];
    for (i = 0U; i < 64U; i++) {
        uint32_t s1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + sha_k[i] + w[i];
        uint32_t s0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + mj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
    st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}

/**
 * @brief SHA-256 of one short message (up to 55 bytes: single block).
 *
 * The chain below only hashes 40-byte inputs, so the single-block form is
 * the whole need and keeps the payload free of streaming state.
 */
static void sha_short(const uint8_t *msg, uint32_t len, uint8_t out[32])
{
    uint32_t st[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    uint8_t blk[64];
    uint32_t i;

    for (i = 0U; i < len; i++) {
        blk[i] = msg[i];
    }
    blk[len] = 0x80U;
    for (i = len + 1U; i < 62U; i++) {
        blk[i] = 0U;
    }
    blk[62] = (uint8_t)((len * 8U) >> 8);
    blk[63] = (uint8_t)(len * 8U);
    sha_block(st, blk);
    for (i = 0U; i < 8U; i++) {
        out[i * 4U]      = (uint8_t)(st[i] >> 24);
        out[i * 4U + 1U] = (uint8_t)(st[i] >> 16);
        out[i * 4U + 2U] = (uint8_t)(st[i] >> 8);
        out[i * 4U + 3U] = (uint8_t)st[i];
    }
}

void tiku_cpu1_sha256_chain(const uint8_t seed[40], uint32_t iters,
                            uint8_t out[32])
{
    uint8_t buf[40];
    uint32_t i;

    for (i = 0U; i < 40U; i++) {
        buf[i] = seed[i];
    }
    if (iters == 0U) {
        iters = 1U;
    }
    while (iters--) {
        sha_short(buf, 40U, out);
        for (i = 0U; i < 32U; i++) {
            buf[i] = out[i];
        }
    }
}
