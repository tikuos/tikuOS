/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cpu1.c - "cpu1" command (RA8P1 Cortex-M33).
 *
 * Starts, halts and reports the second core, reading liveness out of the
 * shared page rather than from what the loader believes it did.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include <stdint.h>
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_io.h>
#include "tiku_shell_cmd_util.h"
#include "tiku_shell_cmd_cpu1.h"

#if (TIKU_SHELL_CMD_CPU1 + 0)

#include <kernel/timers/tiku_htimer.h>
#include <kernel/timers/tiku_clock.h>
#include <interfaces/coproc/tiku_coproc.h>
#include <arch/ra8p1/tiku_cpu1_arch.h>
#include <arch/ra8p1/cpu1/tiku_cpu1_ipc.h>
#include <arch/ra8p1/cpu1/tiku_cpu1_sha256.h>
#include <tikukits/crypto/p256/tiku_kits_crypto_p256.h>

/** @brief Kernel ticks to milliseconds (128 Hz tick). */
#define CPU1_TICKS_TO_MS(t)  (((t) * 125u) / 16u)

/**
 * @brief Wait for the mailbox reply to a work message, without a deadline
 *        shorter than the work.
 *
 * @param out  Digest destination
 * @param cap  Its size
 * @param ms   Budget in milliseconds
 * @return Reply length, or 0 when the budget ran out
 */
static uint32_t cpu1_wait_reply(void *out, uint32_t cap, uint32_t ms)
{
    unsigned long t0 = tiku_clock_time();

    while (CPU1_TICKS_TO_MS(tiku_clock_time() - t0) < ms) {
        uint32_t i;

        if (tiku_coproc_poll()) {
            uint32_t n = tiku_coproc_reply(out, cap);

            if (n != 0u) {
                return n;
            }
        }
        /* Space the polls out: each one invalidates shared lines, and a
         * tight loop on this core starves the other's uncached fetches on
         * the same SRAM. */
        for (i = 0u; i < 50000u; i++) {
            __asm__ volatile ("nop");
        }
    }
    return 0u;
}

/**
 * @brief Handle `cpu1 start|stop|ping|info`.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_cpu1(uint8_t argc, const char *argv[])
{
    if (argc >= 2u && tiku_cmd_streq(argv[1], "start")) {
        int rc = tiku_ra8p1_cpu1_start();

        if (rc != TIKU_RA8P1_CPU1_OK) {
            SHELL_PRINTF("cpu1: start failed (%d)\n", rc);
            return;
        }
        SHELL_PRINTF("cpu1: running; `freq` is pinned until reset\n");
        return;
    }

    if (argc >= 2u && tiku_cmd_streq(argv[1], "stop")) {
        tiku_ra8p1_cpu1_stop();
        SHELL_PRINTF("cpu1: halted\n");
        return;
    }

    if (argc >= 2u && tiku_cmd_streq(argv[1], "ping")) {
        uint8_t probe[8] = { 'T','I','K','U', 0, 0, 0, 0 };
        uint8_t back[8];
        uint32_t rounds = (argc >= 3u) ? (uint32_t)tiku_cmd_parse_u32(argv[2]) : 8u;
        uint32_t ok = 0u, bad = 0u, worst = 0u, total = 0u, r;

        if (rounds == 0u || rounds > 1000u) {
            rounds = 8u;
        }
        for (r = 0u; r < rounds; r++) {
            uint32_t t0;
            uint32_t spins, us, n = 0u;

            /* A different payload every round, so a stale buffer cannot
             * pass for a fresh reply. */
            probe[4] = (uint8_t)(r >> 24); probe[5] = (uint8_t)(r >> 16);
            probe[6] = (uint8_t)(r >> 8);  probe[7] = (uint8_t)r;

            t0 = (uint32_t)tiku_htimer_arch_now();
            if (tiku_coproc_send(probe, sizeof(probe)) !=
                TIKU_COPROC_OK) {
                SHELL_PRINTF("cpu1: send refused (not running?)\n");
                return;
            }
            /* Bounded: an unanswered mailbox must report, not hang the
             * shell that asked. */
            for (spins = 0u; spins < 200000u; spins++) {
                if (tiku_coproc_poll()) {
                    n = tiku_coproc_reply(back, sizeof(back));
                    if (n != 0u) {
                        break;
                    }
                }
            }
            us = (uint32_t)tiku_htimer_arch_now() - t0;
            if (n == sizeof(probe) && back[0] == 'T' && back[3] == 'U' &&
                back[4] == probe[4] && back[5] == probe[5] &&
                back[6] == probe[6] && back[7] == probe[7]) {
                ok++;
                total += us;
                if (us > worst) {
                    worst = us;
                }
            } else {
                bad++;
            }
        }
        SHELL_PRINTF("cpu1: %lu/%lu echoed", (unsigned long)ok,
                     (unsigned long)rounds);
        if (ok != 0u) {
            SHELL_PRINTF(", %lu us mean, %lu us worst",
                         (unsigned long)(total / ok), (unsigned long)worst);
        }
        SHELL_PRINTF("\n");
        return;
    }

    if (argc >= 2u && tiku_cmd_streq(argv[1], "bench")) {
        uint8_t job[TIKU_CPU1_MSG_CAP];
        uint8_t d_in[32], d_off[32], d_par[32];
        uint32_t iters = (argc >= 3u) ? tiku_cmd_parse_u32(argv[2]) : 100000u;
        unsigned long t0;
        uint32_t ms_in, ms_off, ms_par, n, i;
        int match;

        if (argc < 3u) {
            iters = 100000u;
        }
        if (iters > TIKU_CPU1_WORK_MAX_ITERS) {
            iters = TIKU_CPU1_WORK_MAX_ITERS;
        }
        job[0] = (uint8_t)TIKU_CPU1_WORK_MAGIC0;
        job[1] = (uint8_t)TIKU_CPU1_WORK_MAGIC1;
        job[2] = (uint8_t)TIKU_CPU1_WORK_MAGIC2;
        job[3] = (uint8_t)TIKU_CPU1_WORK_MAGIC3;
        job[4] = (uint8_t)iters;        job[5] = (uint8_t)(iters >> 8);
        job[6] = (uint8_t)(iters >> 16); job[7] = (uint8_t)(iters >> 24);
        for (i = 8u; i < TIKU_CPU1_MSG_CAP; i++) {
            job[i] = (uint8_t)i;
        }

        /* Inline: the M85 runs the identical source. */
        t0 = tiku_clock_time();
        tiku_cpu1_sha256_chain(&job[8], iters, d_in);
        ms_in = CPU1_TICKS_TO_MS(tiku_clock_time() - t0);

        /* Offloaded: same job through the mailbox. */
        t0 = tiku_clock_time();
        if (tiku_coproc_send(job, sizeof(job)) != TIKU_COPROC_OK) {
            SHELL_PRINTF("cpu1: send refused (not running?)\n");
            return;
        }
        n = cpu1_wait_reply(d_off, sizeof(d_off), 120000u);
        ms_off = CPU1_TICKS_TO_MS(tiku_clock_time() - t0);
        if (iters == 0u) {
            SHELL_PRINTF("cpu1: seedback n=%lu first %02x%02x%02x%02x\n",
                         (unsigned long)n,
                         d_off[0], d_off[1], d_off[2], d_off[3]);
            return;
        }
        if (n != 32u) {
            SHELL_PRINTF("cpu1: no reply within budget\n");
            return;
        }
        match = 1;
        for (i = 0u; i < 32u; i++) {
            if (d_in[i] != d_off[i]) {
                match = 0;
            }
        }

        /* Parallel: hand CPU1 the job, then do the same work inline while
         * it runs -- two chains for little more than the slower one. */
        t0 = tiku_clock_time();
        (void)tiku_coproc_send(job, sizeof(job));
        tiku_cpu1_sha256_chain(&job[8], iters, d_par);
        n = cpu1_wait_reply(d_off, sizeof(d_off), 120000u);
        ms_par = CPU1_TICKS_TO_MS(tiku_clock_time() - t0);

        if (!match) {
            SHELL_PRINTF("cpu1: in %02x%02x%02x%02x off %02x%02x%02x%02x\n",
                         d_in[0], d_in[1], d_in[2], d_in[3],
                         d_off[0], d_off[1], d_off[2], d_off[3]);
        }
        SHELL_PRINTF("cpu1: %lu iters: inline %lu ms, offload %lu ms, "
                     "digest %s\n",
                     (unsigned long)iters, (unsigned long)ms_in,
                     (unsigned long)ms_off, match ? "MATCH" : "MISMATCH");
        SHELL_PRINTF("      parallel: 2 chains in %lu ms vs %lu ms serial; "
                     "M85's chain rode along for %lu ms extra\n",
                     (unsigned long)ms_par,
                     (unsigned long)(ms_in + ms_off),
                     (unsigned long)(ms_par - (ms_off > ms_in ? ms_off
                                                             : ms_in)));
        return;
    }

    if (argc >= 2u && tiku_cmd_streq(argv[1], "verify")) {
        /* NIST CAVP P-256 SHA-256 vector: a known-good signature, so a
         * wrong answer is a failure rather than an opinion. */
        static const uint8_t qx[32] = {
            0x1c,0xcb,0xe9,0x1c,0x07,0x5f,0xc7,0xf4,0xf0,0x33,0xbf,0xa2,
            0x48,0xdb,0x8f,0xcc,0xd3,0x56,0x5d,0xe9,0x4b,0xbf,0xb1,0x2f,
            0x3c,0x59,0xff,0x46,0xc2,0x71,0xbf,0x83 };
        static const uint8_t qy[32] = {
            0xce,0x40,0x14,0xc6,0x88,0x11,0xf9,0xa2,0x1a,0x1f,0xdb,0x2c,
            0x0e,0x61,0x13,0xe0,0x6d,0xb7,0xca,0x93,0xb7,0x40,0x4e,0x78,
            0xdc,0x7c,0xcd,0x5c,0xa8,0x9a,0x4c,0xa9 };
        static const uint8_t hsh[32] = {
            0x44,0xac,0xf6,0xb7,0xe3,0x6c,0x13,0x42,0xc2,0xc5,0x89,0x72,
            0x04,0xfe,0x09,0x50,0x4e,0x1e,0x2e,0xfb,0x1a,0x90,0x03,0x77,
            0xdb,0xc4,0xe7,0xa6,0xa1,0x33,0xec,0x56 };
        static const uint8_t sr[32] = {
            0xf3,0xac,0x80,0x61,0xb5,0x14,0x79,0x5b,0x88,0x43,0xe3,0xd6,
            0x62,0x95,0x27,0xed,0x2a,0xfd,0x6b,0x1f,0x6a,0x55,0x5a,0x7a,
            0xca,0xbb,0x5e,0x6f,0x79,0xc8,0xc2,0xac };
        static const uint8_t ss[32] = {
            0x8b,0xf7,0x78,0x19,0xca,0x05,0xa6,0xb2,0x78,0x6c,0x76,0x26,
            0x2b,0xf7,0x37,0x1c,0xef,0x97,0xb2,0x18,0xe9,0x6f,0x17,0x5a,
            0x3c,0xcd,0xda,0x2a,0xcc,0x05,0x89,0x03 };
        uint8_t job[TIKU_CPU1_VERIFY_LEN];
        uint8_t rep[8];
        uint32_t rounds = (argc >= 3u) ? tiku_cmd_parse_u32(argv[2]) : 20u;
        unsigned long t0;
        uint32_t ms_in, ms_off, ms_par, n, r, i;
        int in_ok = 1, off_ok = 1;

        if (rounds == 0u || rounds > 500u) {
            rounds = 20u;
        }
        job[0] = (uint8_t)TIKU_CPU1_VERIFY_MAGIC0;
        job[1] = (uint8_t)TIKU_CPU1_VERIFY_MAGIC1;
        job[2] = (uint8_t)TIKU_CPU1_VERIFY_MAGIC2;
        job[3] = (uint8_t)TIKU_CPU1_VERIFY_MAGIC3;
        for (i = 0u; i < 32u; i++) {
            job[4u + i]        = qx[i];
            job[36u + i]       = qy[i];
            job[68u + i]       = hsh[i];
            job[100u + i]      = sr[i];
            job[132u + i]      = ss[i];
        }

        t0 = tiku_clock_time();
        for (r = 0u; r < rounds; r++) {
            if (tiku_kits_crypto_p256_ecdsa_verify(qx, qy, hsh, 32u, sr, ss)
                != 0) {
                in_ok = 0;
            }
        }
        ms_in = CPU1_TICKS_TO_MS(tiku_clock_time() - t0);

        t0 = tiku_clock_time();
        for (r = 0u; r < rounds; r++) {
            if (tiku_coproc_send(job, sizeof(job)) != TIKU_COPROC_OK) {
                SHELL_PRINTF("cpu1: send refused (not running?)\n");
                return;
            }
            n = cpu1_wait_reply(rep, sizeof(rep), 30000u);
            if (n != 1u || rep[0] != 1u) {
                off_ok = 0;
            }
        }
        ms_off = CPU1_TICKS_TO_MS(tiku_clock_time() - t0);

        /* Split the batch: each core verifies half, concurrently. */
        t0 = tiku_clock_time();
        for (r = 0u; r < rounds / 2u; r++) {
            (void)tiku_coproc_send(job, sizeof(job));
            (void)tiku_kits_crypto_p256_ecdsa_verify(qx, qy, hsh, 32u, sr, ss);
            (void)cpu1_wait_reply(rep, sizeof(rep), 30000u);
        }
        ms_par = CPU1_TICKS_TO_MS(tiku_clock_time() - t0);

        SHELL_PRINTF("cpu1: %lu P-256 verifies: inline %lu ms, offload %lu ms"
                     " (%s)\n",
                     (unsigned long)rounds, (unsigned long)ms_in,
                     (unsigned long)ms_off,
                     (in_ok && off_ok) ? "both agree, signature valid"
                                       : "VERDICT MISMATCH");
        SHELL_PRINTF("      split across both cores: %lu ms for the same "
                     "%lu verifies\n",
                     (unsigned long)ms_par, (unsigned long)rounds);

        /* A verifier that always answered "valid" would score identically
         * above, so corrupt the signature and require both to reject. */
        job[100] ^= 0x01u;
        in_ok = (tiku_kits_crypto_p256_ecdsa_verify(qx, qy, hsh, 32u,
                                                    &job[100], ss) != 0);
        off_ok = 0;
        if (tiku_coproc_send(job, sizeof(job)) == TIKU_COPROC_OK) {
            n = cpu1_wait_reply(rep, sizeof(rep), 30000u);
            off_ok = (n == 1u && rep[0] == 0u);
        }
        SHELL_PRINTF("      tampered signature: inline %s, offload %s\n",
                     in_ok ? "rejected" : "ACCEPTED",
                     off_ok ? "rejected" : "ACCEPTED");
        return;
    }

    if (argc < 2u || tiku_cmd_streq(argv[1], "info")) {
        uint32_t a = tiku_ra8p1_cpu1_heartbeat();
        uint32_t b;

        /* Two reads with work between them: a counter that is merely NON-ZERO
         * proves the payload ran once, and one that MOVES proves the core is
         * still executing rather than stopped at a fault. */
        for (b = 0; b < 20000u; b++) {
            __asm__ volatile ("nop");
        }
        b = tiku_ra8p1_cpu1_heartbeat();

        SHELL_PRINTF("cpu1: %s, payload %s, magic %lx\n",
                     tiku_ra8p1_cpu1_active() ? "active" : "power-gated",
                     tiku_ra8p1_cpu1_running() ? "running" : "halted",
                     (unsigned long)tiku_ra8p1_cpu1_magic());
        SHELL_PRINTF("      heartbeat %lu -> %lu (%s), nmi %lu\n",
                     (unsigned long)a, (unsigned long)b,
                     (b != a) ? "advancing" : "STOPPED",
                     (unsigned long)tiku_ra8p1_cpu1_nmi_count);
        SHELL_PRINTF("      faults %lu, doorbells %lu\n",
                     (unsigned long)tiku_ra8p1_cpu1_fault_count,
                     (unsigned long)tiku_ra8p1_cpu1_bell_count);
        {
            uint32_t raw[5];

            tiku_ra8p1_cpu1_raw(raw);
            SHELL_PRINTF("      page halt=%lu restart=%lu seq=%lu "
                         "magic=%lx hb=%lu\n",
                         (unsigned long)raw[0], (unsigned long)raw[1],
                         (unsigned long)raw[2], (unsigned long)raw[3],
                         (unsigned long)raw[4]);
        }
        return;
    }

    SHELL_PRINTF("usage: cpu1 start|stop|ping|bench|verify|info\n");
}

#endif /* TIKU_SHELL_CMD_CPU1 */
