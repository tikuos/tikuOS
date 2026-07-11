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

    if (argc >= 2 && strcmp(argv[1], "dbg") == 0) {
        uint32_t ints, st, sv, stage;
        tiku_crypto_arch_dbg(&ints, &st, &sv, &stage);
        SHELL_PRINTF("ints=0x%08lx status=0x%08lx seedvalid=%lu stage=%lu\n",
                     (unsigned long)ints, (unsigned long)st,
                     (unsigned long)sv, (unsigned long)stage);
        return;
    }

    SHELL_PRINTF("usage: cryptoprobe hwcfg | sha <hexcfg> | sweep | bench | dbg\n");
}

#endif /* TIKU_SHELL_CMD_CRYPTOPROBE */
