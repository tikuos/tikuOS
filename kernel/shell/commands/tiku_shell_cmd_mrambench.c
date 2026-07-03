/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mrambench.c - "mrambench" command implementation (Ambiq).
 *
 * Only compiled on Ambiq (Makefile-gated), so it references the Ambiq-only
 * arch bench directly.  Two modes:
 *   mrambench          time the bootrom MRAM programmer at several spans.
 *   mrambench verify   assert the flush dirty-check skips idle flushes
 *                      (the P0 win): idle flush programs MRAM 0 times, a real
 *                      .uninit change programs exactly once.
 *
 * Both bracket the bench/flush in the NVM unlock window the bootrom program
 * requires; the closing lock_nvm re-commits the mirror (its dirty-check skips
 * the program when .uninit did not change).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_mrambench.h"
#include <kernel/shell/tiku_shell.h>       /* SHELL_PRINTF */
#include <kernel/memory/tiku_mem.h>        /* tiku_mpu_unlock_nvm / lock_nvm */
#include <arch/ambiq/tiku_mram_bench.h>    /* bench + program-count getter    */
#include <string.h>                        /* strcmp                          */

/* A scratch word in .uninit (the mirrored region) so `verify` can force a
 * real dirty-check hit without touching any live persist cell. */
static volatile uint32_t __attribute__((section(".uninit"))) s_mram_test_scratch;

/*---------------------------------------------------------------------------*/
/* mrambench verify -- dirty-check regression guard                          */
/*---------------------------------------------------------------------------*/

static void
mrambench_verify(void)
{
    uint32_t c0, c1, c2, c3;
    uint16_t saved;

    /* Baseline: one flush so the mirror matches current .uninit -- the next
     * idle flush then has nothing to program. */
    saved = tiku_mpu_unlock_nvm();
    tiku_mpu_lock_nvm(saved);

    /* (1) Idle flush: no .uninit change -> the dirty-check must skip. */
    c0 = tiku_mem_arch_nvm_program_count();
    saved = tiku_mpu_unlock_nvm();
    tiku_mpu_lock_nvm(saved);
    c1 = tiku_mem_arch_nvm_program_count();

    /* (2) Change .uninit inside the unlock window, flush -> must program once. */
    saved = tiku_mpu_unlock_nvm();
    s_mram_test_scratch ^= 0xDEADBEEFuL;
    tiku_mpu_lock_nvm(saved);
    c2 = tiku_mem_arch_nvm_program_count();

    /* (3) Idle flush again -> must skip. */
    saved = tiku_mpu_unlock_nvm();
    tiku_mpu_lock_nvm(saved);
    c3 = tiku_mem_arch_nvm_program_count();

    SHELL_PRINTF("mrambench verify: idle0=%lu change=%lu idle1=%lu\n",
                 (unsigned long)(c1 - c0), (unsigned long)(c2 - c1),
                 (unsigned long)(c3 - c2));
    if ((c1 == c0) && (c2 == c1 + 1U) && (c3 == c2)) {
        SHELL_PRINTF("mrambench verify: PASS "
                     "(dirty-check skips idle flush, commits real change)\n");
    } else {
        SHELL_PRINTF("mrambench verify: FAIL "
                     "(expected idle0=0 change=1 idle1=0)\n");
    }
}

/*---------------------------------------------------------------------------*/
/* mrambench (no arg) -- program-timing benchmark                            */
/*---------------------------------------------------------------------------*/

static void
mrambench_time(void)
{
    tiku_mem_nvm_bench_row_t rows[4];
    unsigned long dwt_hz = 0UL, tpus;
    uint16_t saved;
    uint8_t  n, i;

    saved = tiku_mpu_unlock_nvm();
    n = tiku_mem_arch_nvm_bench(rows,
                                (uint8_t)(sizeof(rows) / sizeof(rows[0])),
                                &dwt_hz);
    tiku_mpu_lock_nvm(saved);

    if (n == 0U) {
        SHELL_PRINTF("mrambench: no safe scratch (.uninit image too large)\n");
        return;
    }

    tpus = dwt_hz / 1000000UL;   /* DWT ticks per microsecond */
    SHELL_PRINTF("MRAM program (bootrom nv_program_main2), best of 4:\n");
    SHELL_PRINTF("  DWT rate %lu Hz (~%lu ticks/us)\n", dwt_hz, tpus);
    for (i = 0U; i < n; i++) {
        unsigned long us = tpus ? ((unsigned long)rows[i].cycles / tpus) : 0UL;
        SHELL_PRINTF("  bytes=%u words=%lu cycles=%lu us=%lu\n",
                     (unsigned)rows[i].bytes,
                     (unsigned long)(rows[i].bytes / 4U),
                     (unsigned long)rows[i].cycles, us);
    }

    /* Two-point fit (smallest vs largest span): fixed per-call overhead and
     * the marginal per-word cost -- what a block-granular flush is sized on. */
    if (n >= 2U) {
        uint32_t w0 = (uint32_t)rows[0].bytes / 4U;
        uint32_t w1 = (uint32_t)rows[n - 1U].bytes / 4U;
        if (w1 > w0 && rows[n - 1U].cycles >= rows[0].cycles) {
            unsigned long per_word =
                (unsigned long)(rows[n - 1U].cycles - rows[0].cycles)
                / (unsigned long)(w1 - w0);
            unsigned long pw_w0    = per_word * (unsigned long)w0;
            unsigned long overhead =
                ((unsigned long)rows[0].cycles > pw_w0)
                    ? ((unsigned long)rows[0].cycles - pw_w0) : 0UL;
            SHELL_PRINTF("  fit: overhead ~%lu cyc, per-word ~%lu cyc\n",
                         overhead, per_word);
            if (tpus) {
                SHELL_PRINTF("       overhead ~%lu us, per-KB ~%lu us\n",
                             overhead / tpus, (per_word * 256UL) / tpus);
            }
        }
    }
}

void
tiku_shell_cmd_mrambench(uint8_t argc, const char *argv[])
{
    if (argc >= 2U && strcmp(argv[1], "verify") == 0) {
        mrambench_verify();
        return;
    }
    mrambench_time();
}
