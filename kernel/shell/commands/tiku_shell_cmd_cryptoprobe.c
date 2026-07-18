/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cryptoprobe.c - CRACEN CryptoMaster bring-up probe.
 *
 * Interactive diagnostic for the hardware-crypto backend, in the nvmprobe
 * mold (opt-in, TIKU_SHELL_CMD_CRYPTOPROBE=1 via EXTRA_CFLAGS):
 *
 *   cryptoprobe hwcfg        dump the CRYPTMSTRHW fused-engine words
 *   cryptoprobe sha <hexcfg> hash "abc" with that BA413 config word,
 *                            print the digest + wall time + vector verdict
 *   cryptoprobe sweep        iterate candidate config words until one
 *                            reproduces SHA-256("abc") -- the empirical
 *                            way to pin the engine's config encoding
 *   cryptoprobe bench        time hw vs sw SHA-256 over 4 KB
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_cryptoprobe.h"

#if TIKU_SHELL_CMD_CRYPTOPROBE

#include <kernel/shell/tiku_shell_io.h>
#include <arch/nordic/tiku_timer_arch.h>  /* TIKU_CLOCK_ARCH_SECOND before clock.h */
#include <kernel/timers/tiku_clock.h>
#include <arch/nordic/tiku_crypto_arch.h>
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>
#include <tikukits/crypto/gcm/tiku_kits_crypto_gcm.h>
#include <tikukits/crypto/p256/tiku_kits_crypto_p256.h>
#include <string.h>
#include <stdlib.h>

/* FIPS 180-4: SHA-256("abc") */
static const uint8_t sha256_abc[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
};

static void print_digest(const uint8_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        SHELL_PRINTF("%02x", d[i]);
    }
}

static void probe_one(uint32_t cfg, uint8_t quiet)
{
    uint8_t  out[32];
    uint32_t t0, t1;
    int      rc;

    memset(out, 0, sizeof out);
    t0 = tiku_clock_arch_fine();
    rc = tiku_crypto_arch_hash_probe(cfg, "abc", 3u, out, sizeof out);
    t1 = tiku_clock_arch_fine();

    if (memcmp(out, sha256_abc, sizeof out) == 0) {
        SHELL_PRINTF(SH_GREEN "cfg=0x%08lx MATCH SHA-256" SH_RST
                     " (%lu us)\n", (unsigned long)cfg,
                     (unsigned long)(t1 - t0));
    } else if (!quiet) {
        SHELL_PRINTF("cfg=0x%08lx rc=%d digest=", (unsigned long)cfg, rc);
        print_digest(out, 8u);          /* first 8 bytes tell the story */
        SHELL_PRINTF("... (%lu us)\n", (unsigned long)(t1 - t0));
    }
}

void tiku_shell_cmd_cryptoprobe(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "hwcfg") == 0) {
        static const char *names[7] = {
            "INCLIPS", "BA411CFG1", "BA411CFG2", "BA413HASH",
            "BA418SHA3", "BA419SM4", "BA424ARIA",
        };
        uint8_t i;
        for (i = 0; i < 7u; i++) {
            SHELL_PRINTF("  %-10s = 0x%08lx\n", names[i],
                         (unsigned long)tiku_crypto_arch_hwcfg(i));
        }
        return;
    }

    if (argc >= 3 && strcmp(argv[1], "sha") == 0) {
        probe_one((uint32_t)strtoul(argv[2], (char **)0, 16), 0u);
        return;
    }

    /* FIPS-197 C.1 AES-128 ECB single-block known-answer test. */
    if (argc >= 2 && strcmp(argv[1], "ecb") == 0) {
        static const uint8_t key[16] = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };
        static const uint8_t pt[16] = {
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
        static const uint8_t ct[16] = {
            0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
            0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a };
        uint8_t out[16], back[16];
        int rc = tiku_crypto_arch_aes_ecb(0, key, 16u, pt, out);
        SHELL_PRINTF("ECB enc rc=%d out=", rc);
        print_digest(out, 16u);
        SHELL_PRINTF(memcmp(out, ct, 16u) == 0 ? SH_GREEN " MATCH\n" SH_RST
                                               : SH_RED " MISMATCH\n" SH_RST);
        rc = tiku_crypto_arch_aes_ecb(1, key, 16u, ct, back);
        SHELL_PRINTF("ECB dec rc=%d %s\n", rc,
                     memcmp(back, pt, 16u) == 0 ? "roundtrip OK" : "FAIL");
        return;
    }

    /* RFC 3610 packet vector #1: AES-CCM* known-answer (M=8, L=2). */
    if (argc >= 2 && strcmp(argv[1], "ccm") == 0) {
        static const uint8_t key[16] = {
            0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,
            0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf };
        static const uint8_t nonce[13] = {
            0x00,0x00,0x00,0x03,0x02,0x01,0x00,0xa0,0xa1,0xa2,0xa3,0xa4,0xa5 };
        static const uint8_t aad[8] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07 };
        static const uint8_t msg[23] = {
            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,
            0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e };
        static const uint8_t ct[23] = {
            0x58,0x8c,0x97,0x9a,0x61,0xc6,0x63,0xd2,0xf0,0x66,0xd0,0xc2,
            0xc0,0xf9,0x89,0x80,0x6d,0x5f,0x6b,0x61,0xda,0xc3,0x84 };
        static const uint8_t tag[8] = {
            0x17,0xe8,0xd1,0x2c,0xfd,0xf9,0x26,0xe0 };
        uint8_t out[23], mic[8], back[23];
        int rc = tiku_crypto_arch_aes_ccm_star(0, key, 16u, nonce, aad, 8u,
                                               msg, 23u, 8u, out, mic);
        int ok = (rc == 0 && memcmp(out, ct, 23u) == 0 &&
                  memcmp(mic, tag, 8u) == 0);
        SHELL_PRINTF("CCM* enc rc=%d ct=", rc);
        print_digest(out, 8u);
        SHELL_PRINTF(".. mic=");
        print_digest(mic, 8u);
        SHELL_PRINTF(ok ? SH_GREEN " MATCH (RFC3610 #1)\n" SH_RST
                        : SH_RED " MISMATCH\n" SH_RST);
        rc = tiku_crypto_arch_aes_ccm_star(1, key, 16u, nonce, aad, 8u,
                                           ct, 23u, 8u, back, mic);
        SHELL_PRINTF("CCM* dec rc=%d %s\n", rc,
                     (rc == 0 && memcmp(back, msg, 23u) == 0 &&
                      memcmp(mic, tag, 8u) == 0) ? "verify OK" : "FAIL");
        return;
    }

    if (argc >= 2 && strcmp(argv[1], "sweep") == 0) {
        /* Candidate space: mode nibble (index- or one-hot-coded) x the
         * plausible hw-padding/final control bits seen on BA41x-family
         * engines.  ~100 combos, microseconds each. */
        static const uint32_t modes[] = {
            0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x10, 0x20, 0x40,
        };
        static const uint32_t ctrls[] = {
            0x000, 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700,
        };
        uint8_t m, c;
        SHELL_PRINTF("sweeping %u configs against SHA-256(\"abc\")...\n",
                     (unsigned)(sizeof modes / 4 * (sizeof ctrls / 4)));
        for (m = 0; m < sizeof modes / 4; m++) {
            for (c = 0; c < sizeof ctrls / 4; c++) {
                probe_one(modes[m] | ctrls[c], 1u);
            }
        }
        SHELL_PRINTF("sweep done (silent = no match)\n");
        return;
    }

    if (argc >= 2 && strcmp(argv[1], "bench") == 0) {
        /* Iterate each path over 4 KB and count kernel ticks -- coarse per
         * tick (7.8 ms) but honest; the iteration count divides it down to
         * us-per-op.  (The GRTC fine capture proved unreliable for short
         * deltas; ticks x N is wraparound-proof.) */
        static uint8_t buf[4096];
        uint8_t  hw[32], sw[32];
        tiku_clock_time_t t0;
        uint32_t i, n_hw = 512u, n_sw = 16u, thw, tsw;
        int      rc = 0;
        for (i = 0; i < sizeof buf; i++) { buf[i] = (uint8_t)i; }

        t0 = tiku_clock_time();
        for (i = 0; i < n_hw && rc == 0; i++) {
            rc = tiku_crypto_arch_sha256(buf, sizeof buf, hw);
        }
        thw = (uint32_t)((tiku_clock_time_t)(tiku_clock_time() - t0));

        tiku_crypto_hw_mode_set(TIKU_CRYPTO_HW_MODE_SW);
        t0 = tiku_clock_time();
        for (i = 0; i < n_sw; i++) {
            tiku_kits_crypto_sha256_hash(buf, sizeof buf, sw);
        }
        tsw = (uint32_t)((tiku_clock_time_t)(tiku_clock_time() - t0));
        tiku_crypto_hw_mode_set(TIKU_CRYPTO_HW_MODE_AUTO);

        SHELL_PRINTF("sha256(4KB): hw %lu us/op (%lu ticks/%lu), "
                     "sw %lu us/op (%lu ticks/%lu), rc=%d %s\n",
                     (unsigned long)(thw * (1000000UL / TIKU_CLOCK_SECOND)
                                     / n_hw),
                     (unsigned long)thw, (unsigned long)n_hw,
                     (unsigned long)(tsw * (1000000UL / TIKU_CLOCK_SECOND)
                                     / n_sw),
                     (unsigned long)tsw, (unsigned long)n_sw,
                     rc,
                     (rc == 0 && memcmp(hw, sw, 32) == 0)
                         ? SH_GREEN "digests MATCH" SH_RST
                         : SH_RED "MISMATCH" SH_RST);
        return;
    }

    if (argc >= 2 && strcmp(argv[1], "bypass") == 0) {
        uint8_t out[8];
        int rc;
        memset(out, 0, sizeof out);
        rc = tiku_crypto_arch_bypass_probe("TIKUBYPS", 8u, out);
        SHELL_PRINTF("bypass rc=%d out=%c%c%c%c%c%c%c%c %s\n", rc,
                     out[0], out[1], out[2], out[3],
                     out[4], out[5], out[6], out[7],
                     (rc == 0 && memcmp(out, "TIKUBYPS", 8) == 0)
                         ? SH_GREEN "MATCH" SH_RST : SH_RED "FAIL" SH_RST);
        return;
    }

    if (argc >= 2 && strcmp(argv[1], "direct") == 0) {
        static uint8_t out[8];
        int rc;
        memset(out, 0, sizeof out);
        rc = tiku_crypto_arch_direct_probe("TIKUDRCT", 8u, out);
        SHELL_PRINTF("direct rc=%d out=%c%c%c%c%c%c%c%c %s\n", rc,
                     out[0], out[1], out[2], out[3],
                     out[4], out[5], out[6], out[7],
                     (rc == 0 && memcmp(out, "TIKUDRCT", 8) == 0)
                         ? SH_GREEN "MATCH" SH_RST : SH_RED "FAIL" SH_RST);
        return;
    }

    if (argc >= 3 && strcmp(argv[1], "mode") == 0) {
        tiku_crypto_hw_mode_set(
            (strcmp(argv[2], "sw") == 0) ? TIKU_CRYPTO_HW_MODE_SW
                                         : TIKU_CRYPTO_HW_MODE_AUTO);
        SHELL_PRINTF("crypto mode = %s\n",
                     tiku_crypto_hw_mode() ? "sw" : "auto");
        return;
    }

    if (argc >= 2 && strcmp(argv[1], "counters") == 0) {
        uint16_t h, sc, e;
        tiku_crypto_hw_counters(&h, &sc, &e);
        SHELL_PRINTF("hw_ops=%u sw_ops=%u hw_errs=%u mode=%s\n",
                     h, sc, e, tiku_crypto_hw_mode() ? "sw" : "auto");
        return;
    }

    if (argc >= 2 && strcmp(argv[1], "gcm") == 0) {
        /* hw GCM vs the NIST-vector-proven software kit: AES-256, 12-byte
         * IV, 20-byte AAD, 67-byte payload (odd tail on purpose). */
        static uint8_t key[32], iv[12], aad[32], pt[80], ct_sw[80], ct_hw[80];
        static uint8_t tag_sw[16], tag_hw[16], back[80];
        tiku_kits_crypto_gcm_ctx_t gctx;
        uint32_t extra = (argc >= 3)
            ? (uint32_t)strtoul(argv[2], (char **)0, 16) : 0u;
        size_t i;
        int rc, rc2;
        for (i = 0; i < sizeof key; i++) { key[i] = (uint8_t)(i * 7u + 1u); }
        for (i = 0; i < sizeof iv;  i++) { iv[i]  = (uint8_t)(0xA0u + i); }
        for (i = 0; i < sizeof aad; i++) { aad[i] = (uint8_t)(0x30u + i); }
        for (i = 0; i < 67u;        i++) { pt[i]  = (uint8_t)(i ^ 0x5Au); }

        tiku_kits_crypto_gcm_init256(&gctx, key);
        tiku_kits_crypto_gcm_encrypt(&gctx, iv, aad, 20u, pt, 67u,
                                     ct_sw, tag_sw);
        rc = tiku_crypto_arch_aes_gcm(0, extra, key, 32u, iv,
                                      aad, 20u, pt, 67u, ct_hw, tag_hw);
        SHELL_PRINTF("gcm enc rc=%d ct %s tag %s\n", rc,
                     (rc == 0 && memcmp(ct_sw, ct_hw, 67u) == 0)
                         ? SH_GREEN "MATCH" SH_RST : SH_RED "diff" SH_RST,
                     (rc == 0 && memcmp(tag_sw, tag_hw, 16u) == 0)
                         ? SH_GREEN "MATCH" SH_RST : SH_RED "diff" SH_RST);
        {
            /* differential: does hw look like AES-128 over key[0..15]? */
            static uint8_t ct128[80], tag128[16];
            tiku_kits_crypto_gcm_ctx_t g128;
            tiku_kits_crypto_gcm_init(&g128, key);
            tiku_kits_crypto_gcm_encrypt(&g128, iv, aad, 20u, pt, 67u,
                                         ct128, tag128);
            SHELL_PRINTF("  hw-vs-sw128: ct %s tag %s\n",
                         memcmp(ct128, ct_hw, 67u) == 0 ? "MATCH" : "diff",
                         memcmp(tag128, tag_hw, 16u) == 0 ? "MATCH" : "diff");
            SHELL_PRINTF("  sw ct: ");
            print_digest(ct_sw, 8u);
            SHELL_PRINTF("  hw ct: ");
            print_digest(ct_hw, 8u);
            SHELL_PRINTF("\n  sw tag: ");
            print_digest(tag_sw, 8u);
            SHELL_PRINTF(" hw tag: ");
            print_digest(tag_hw, 8u);
            SHELL_PRINTF("\n");
        }

        rc2 = tiku_crypto_arch_aes_gcm(1, extra, key, 32u, iv,
                                       aad, 20u, ct_sw, 67u, back, tag_hw);
        SHELL_PRINTF("gcm dec rc=%d pt %s tag %s\n", rc2,
                     (rc2 == 0 && memcmp(back, pt, 67u) == 0)
                         ? SH_GREEN "MATCH" SH_RST : SH_RED "diff" SH_RST,
                     (rc2 == 0 && memcmp(tag_hw, tag_sw, 16u) == 0)
                         ? SH_GREEN "MATCH" SH_RST : SH_RED "diff" SH_RST);
        return;
    }

#if defined(TIKU_CRACEN_PK_ENABLE)
    if (argc >= 2 && strcmp(argv[1], "pk") == 0) {
        /* P-256 KAT (openssl prime256v1 / SHA-256) */
        static const uint8_t p256_qx[32] = {
            0xd9,0x76,0xeb,0xbe,0x6a,0xd8,0xc1,0xf5,0x74,0xc0,0x36,0xe6,
            0x1b,0x80,0xb4,0xc5,0x20,0x86,0x01,0x7c,0x66,0xdc,0x51,0xe1,
            0x74,0xa8,0x78,0x40,0x37,0x34,0x9b,0x7a };
        static const uint8_t p256_qy[32] = {
            0x8c,0xcd,0x3a,0x74,0x5c,0x81,0xf4,0x35,0xb0,0x44,0x4d,0x0e,
            0x55,0x2d,0x97,0xfc,0x76,0x9e,0x51,0xba,0x40,0xbf,0xd6,0x43,
            0xc3,0x97,0x07,0x76,0x3b,0x5f,0xe7,0x61 };
        static const uint8_t p256_h[32] = {
            0xfb,0xb7,0x9b,0xc2,0x27,0x6c,0x00,0xc2,0xc1,0x8a,0x13,0x63,
            0xc2,0x93,0x41,0xbf,0xc0,0xe0,0x28,0x0b,0xc9,0xfc,0xe8,0xc8,
            0xf1,0x6a,0x60,0xd4,0x85,0x62,0x89,0x16 };
        static const uint8_t p256_r[32] = {
            0x59,0x21,0x6e,0x66,0x96,0x65,0xde,0x8b,0xdd,0x8f,0x81,0xf5,
            0xb9,0x75,0x61,0x23,0x08,0x04,0xb4,0x55,0x3e,0x23,0x9f,0x47,
            0xf3,0x9a,0x2f,0x66,0x19,0xcb,0xa0,0x21 };
        static const uint8_t p256_s[32] = {
            0xb4,0x3a,0x0e,0x89,0x67,0xaa,0x44,0xef,0x6d,0x4c,0x7c,0x44,
            0x00,0xa4,0x91,0x0b,0xe4,0x1e,0xf7,0x13,0x31,0x4e,0xa3,0x06,
            0xe8,0x36,0xb9,0x68,0x94,0x1b,0xe8,0x4f };
        /* P-384 KAT (matches the kits-crypto-cert vector) */
        static const uint8_t p384_qx[48] = {
            0xb0,0x46,0xd1,0xbd,0x27,0x9a,0x56,0xb7,0xcf,0x78,0x7d,0x74,
            0xfa,0xeb,0x00,0x58,0xd3,0xa9,0xe6,0x56,0x67,0x1f,0x65,0x2f,
            0xc7,0x05,0xd8,0x0e,0xfd,0x64,0x6a,0xa0,0xc2,0x92,0x18,0xeb,
            0x68,0x07,0x10,0xe5,0x01,0x08,0xf0,0xce,0xdb,0xfd,0xaf,0x80 };
        static const uint8_t p384_qy[48] = {
            0xb8,0x71,0x2f,0x42,0xa2,0xc8,0xc4,0x9c,0x11,0x21,0xd7,0x2d,
            0x5c,0x5e,0xe7,0x8c,0x63,0x58,0x01,0x64,0x21,0xb1,0xf5,0x0d,
            0xb8,0xdf,0xee,0x35,0xd5,0xf3,0x29,0x09,0xba,0x6b,0x1f,0x2e,
            0xea,0x3e,0x0a,0x3a,0xb5,0x2d,0xc3,0x99,0x81,0xda,0x55,0x3c };
        static const uint8_t p384_h[48] = {
            0x61,0xda,0xeb,0xff,0xcf,0xbe,0xa1,0x1a,0x22,0x3b,0xa2,0xce,
            0xd0,0xd7,0x37,0x88,0xc7,0x22,0xb5,0xfc,0x7d,0x41,0xfa,0x65,
            0xa8,0x9d,0x41,0x11,0x54,0xa7,0x71,0xf6,0x73,0x4b,0xc8,0xf7,
            0xa5,0x8c,0xfe,0x53,0x65,0x62,0x2a,0x7b,0x4c,0x0c,0x18,0x11 };
        static const uint8_t p384_r[48] = {
            0xbb,0x58,0xae,0x22,0xa2,0x62,0xde,0x02,0xd9,0x62,0xcf,0xe7,
            0xe5,0xd6,0x16,0x2f,0x7b,0x21,0x30,0xf2,0x6b,0xe9,0x9e,0xe4,
            0x9f,0x50,0x56,0xcb,0x42,0x4e,0x16,0x61,0xca,0x4a,0x87,0x70,
            0x0f,0x6d,0x25,0x37,0x8f,0x74,0x26,0xa1,0x02,0x6d,0x03,0xcf };
        static const uint8_t p384_s[48] = {
            0x17,0x9d,0x68,0x79,0x5a,0x42,0xb8,0x7a,0xba,0x0f,0x94,0x69,
            0x43,0xb4,0xd7,0xdf,0x9a,0x02,0x4e,0xaf,0x04,0x86,0xba,0x63,
            0x7f,0x19,0xd6,0x47,0x22,0xbb,0xcf,0x2a,0x21,0x78,0xf8,0xbc,
            0xf2,0x39,0x66,0x83,0xab,0xe5,0x77,0xd1,0x2a,0xd5,0x19,0x26 };
        uint8_t bad[48];
        int v, iv;
        SHELL_PRINTF("pk hwconfig=0x%08lx  ucode[0]=0x%08lx %s\n",
                     (unsigned long)tiku_crypto_arch_pk_hwconfig(),
                     (unsigned long)tiku_crypto_arch_pk_ucode0(),
                     (tiku_crypto_arch_pk_ucode0() == 0u)
                         ? SH_RED "(microcode NOT loaded)" SH_RST : "");

        v  = tiku_crypto_arch_p256_ecdsa_verify(p256_qx, p256_qy, p256_h, 32,
                                                p256_r, p256_s);
        memcpy(bad, p256_s, 32); bad[31] ^= 0x01;
        iv = tiku_crypto_arch_p256_ecdsa_verify(p256_qx, p256_qy, p256_h, 32,
                                                p256_r, bad);
        { uint32_t st,cm,sp,ss; tiku_crypto_arch_pk_dbg(&st,&cm,&sp,&ss);
          SHELL_PRINTF("P-256 ecdsa: v=%d iv=%d code=0x%lx spin=%lu\n",
                       v, iv, (unsigned long)(st & 0x1FFF0u), (unsigned long)sp); }

        v  = tiku_crypto_arch_p384_ecdsa_verify(p384_qx, p384_qy, p384_h, 48,
                                                p384_r, p384_s);
        memcpy(bad, p384_r, 48); bad[47] ^= 0x01;
        iv = tiku_crypto_arch_p384_ecdsa_verify(p384_qx, p384_qy, p384_h, 48,
                                                p384_r, bad);
        SHELL_PRINTF("P-384 ecdsa: valid->%d tampered->%d  %s\n", v, iv,
                     (v == 0 && iv == 1) ? SH_GREEN "PASS" SH_RST
                                         : SH_RED "FAIL" SH_RST);
        {
            uint32_t st, cm, sp, ss;
            tiku_crypto_arch_pk_dbg(&st, &cm, &sp, &ss);
            SHELL_PRINTF("  (hw PK needs the BA414EP microcode, not shipped;"
                         " -1 => software path)  status=0x%lx spin=%lu\n",
                         (unsigned long)st, (unsigned long)sp);
        }
        return;
    }
#endif /* TIKU_CRACEN_PK_ENABLE */

    if (argc >= 2 && strcmp(argv[1], "dbg") == 0) {
        uint32_t ints, st, sv, stage;
        tiku_crypto_arch_dbg(&ints, &st, &sv, &stage);
        SHELL_PRINTF("ints=0x%08lx status=0x%08lx seedvalid=%lu stage=%lu\n",
                     (unsigned long)ints, (unsigned long)st,
                     (unsigned long)sv, (unsigned long)stage);
        return;
    }

    SHELL_PRINTF("usage: cryptoprobe hwcfg|sha|sweep|gcm|pk|bench|mode|counters|dbg\n");
}

#endif /* TIKU_SHELL_CMD_CRYPTOPROBE */
