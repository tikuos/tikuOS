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
#include <interfaces/coproc/tiku_coproc.h>
#include <arch/ra8p1/tiku_cpu1_arch.h>

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
        return;
    }

    SHELL_PRINTF("usage: cpu1 start|stop|ping [n]|info\n");
}

#endif /* TIKU_SHELL_CMD_CPU1 */
