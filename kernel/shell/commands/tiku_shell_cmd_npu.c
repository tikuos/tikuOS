/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_npu.c - "npu" shell command.
 *
 * Release the Ethos-U55 and report what it says about itself.  Running a
 * command stream on it is not yet wired.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_npu.h"

#if (TIKU_SHELL_CMD_NPU + 0)

#include <kernel/shell/tiku_shell_io.h>
#include <arch/ra8p1/tiku_npu_arch.h>
#include "tiku_shell_cmd_util.h"

/** @brief Map a bring-up return code to something a reader can act on. */
static const char *npu_err(int rc)
{
    switch (rc) {
    case TIKU_RA8P1_NPU_ERR_MOCO:  return "MOCO stopped";
    case TIKU_RA8P1_NPU_ERR_POWER: return "domain stayed gated";
    case TIKU_RA8P1_NPU_ERR_ID:    return "unexpected id";
    case TIKU_RA8P1_NPU_ERR_TIMEOUT: return "stream never ended";
    case TIKU_RA8P1_NPU_ERR_FAULT:   return "parse or bus fault";
    case TIKU_RA8P1_NPU_ERR_MISMATCH: return "output differs";
    case TIKU_RA8P1_NPU_ERR_IMAGE:   return "no usable model";
    case TIKU_RA8P1_NPU_ERR_ARENA:   return "model arena too large";
    default:                       return "unknown";
    }
}

void tiku_shell_cmd_npu(uint8_t argc, const char *argv[])
{
    int rc;

    if (argc >= 2u && argv[1][0] == 'b') {
        uint32_t rounds = (argc >= 3u) ? tiku_cmd_parse_u32(argv[2]) : 20u;
        uint32_t npu_us = 0u, cpu_us = 0u;

        rc = tiku_ra8p1_npu_bench(rounds, &npu_us, &cpu_us);
        if (rc != TIKU_RA8P1_NPU_OK) {
            SHELL_PRINTF("npu: bench failed (%s)\n", npu_err(rc));
            return;
        }
        SHELL_PRINTF("npu %lu us, m85 %lu us over %lu rounds\n",
                     (unsigned long)npu_us, (unsigned long)cpu_us,
                     (unsigned long)rounds);
        if (npu_us != 0u) {
            SHELL_PRINTF("ratio %lu.%02lux in the NPU's favour\n",
                         (unsigned long)(cpu_us / npu_us),
                         (unsigned long)((cpu_us * 100ul / npu_us) % 100ul));
        }
        return;
    }
    if (argc >= 3u && argv[1][0] == 'l') {
        rc = tiku_ra8p1_npu_load(argv[2]);
        SHELL_PRINTF("npu: load %s: %s\n", argv[2],
                     (rc == TIKU_RA8P1_NPU_OK) ? "ok" : npu_err(rc));
        return;
    }
    if (argc >= 2u && argv[1][0] == 'o' && argv[1][1] == 'f') {
        tiku_ra8p1_npu_stop();
        SHELL_PRINTF("npu: gated\n");
        return;
    }

    rc = tiku_ra8p1_npu_init();
    if (rc != TIKU_RA8P1_NPU_OK) {
        SHELL_PRINTF("npu: not available (%s)\n", npu_err(rc));
        return;
    }

    SHELL_PRINTF("id:    0x%08lx\n", (unsigned long)tiku_ra8p1_npu_id());
    SHELL_PRINTF("macs:  %u/cycle\n", (unsigned)tiku_ra8p1_npu_macs());
    SHELL_PRINTF("shram: %u KB\n", (unsigned)tiku_ra8p1_npu_shram_kb());
    SHELL_PRINTF("irqs:  %lu\n",
                 (unsigned long)tiku_ra8p1_npu_irq_count);
    SHELL_PRINTF("model: %ux%u, arena %lu B, stream %lu B, from %s\n",
                 (unsigned)tiku_ra8p1_npu_model()->ifm_dim,
                 (unsigned)tiku_ra8p1_npu_model()->ifm_dim,
                 (unsigned long)tiku_ra8p1_npu_model()->arena,
                 (unsigned long)tiku_ra8p1_npu_model()->cms_len,
                 tiku_ra8p1_npu_from_store() ? "store" : "image");
}

void tiku_shell_cmd_npu_test(uint8_t argc, const char *argv[])
{
    uint32_t sta = 0u;
    uint32_t seed;
    unsigned rounds, i;
    int rc;

    seed = (argc >= 2u) ? (uint32_t)tiku_cmd_parse_u32(argv[1]) : 1u;
    rounds = (argc >= 3u) ? (unsigned)tiku_cmd_parse_u32(argv[2]) : 4u;

    for (i = 0u; i < rounds; i++) {
        rc = tiku_ra8p1_npu_selftest(seed + i, &sta);
        if (rc != TIKU_RA8P1_NPU_OK) {
            SHELL_PRINTF("npu: round %u FAILED (%s) status 0x%08lx\n",
                         i, npu_err(rc), (unsigned long)sta);
            return;
        }
    }
    SHELL_PRINTF("npu: %u/%u rounds match the M85, status 0x%08lx, irqs %lu\n",
                 rounds, rounds, (unsigned long)sta,
                 (unsigned long)tiku_ra8p1_npu_irq_count);

    /* The same check against a corrupted stream, so a pass above means the
     * comparison can distinguish a good run from a bad one. */
    rc = tiku_ra8p1_npu_selftest_tampered(seed);
    SHELL_PRINTF("npu: tampered stream %s\n",
                 (rc == TIKU_RA8P1_NPU_OK) ? "ACCEPTED (check is blind)"
                                           : npu_err(rc));

    /* And the same run with the cache maintenance taken out.  It has to FAIL:
     * a pass would mean the buffers never held dirty lines, and the maintained
     * run above would have proven nothing about coherency. */
    /* And with the completion interrupt masked.  It must FAIL: a pass would
     * mean the run never depended on the interrupt at all. */
    rc = tiku_ra8p1_npu_selftest_noirq(seed);
    SHELL_PRINTF("npu: without the completion irq %s\n",
                 (rc == TIKU_RA8P1_NPU_OK) ? "PASSED (run was not irq-driven)"
                                           : npu_err(rc));

    /* Only meaningful once a model has weights: a wrong answer is the only
     * proof the accelerator reads them, since it cannot check them. */
    rc = tiku_ra8p1_npu_selftest_badwts(seed);
    if (rc != TIKU_RA8P1_NPU_ERR_IMAGE) {
        SHELL_PRINTF("npu: with a corrupted weight %s\n",
                     (rc == TIKU_RA8P1_NPU_OK) ? "PASSED (weights unread)"
                                               : npu_err(rc));
    }

    rc = tiku_ra8p1_npu_selftest_nomaint(seed);
    SHELL_PRINTF("npu: without cache maintenance %s\n",
                 (rc == TIKU_RA8P1_NPU_OK) ? "PASSED (buffers were not cached)"
                                           : npu_err(rc));
}

#endif /* TIKU_SHELL_CMD_NPU */
