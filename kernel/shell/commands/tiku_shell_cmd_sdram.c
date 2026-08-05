/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sdram.c - "sdram" command implementation.
 *
 * Bring-up is a verb rather than boot work: 64 MB of SDRAM draws tens of mA
 * refreshing, which a board that is not using it should not pay.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_sdram.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/memory/tiku_mem.h>
#include <string.h>

#if TIKU_SHELL_CMD_SDRAM

#include <arch/ra8p1/tiku_sdram_arch.h>

void tiku_shell_cmd_sdram(uint8_t argc, const char *argv[])
{
    tiku_mem_stats_t st;

    if (argc >= 2 && strcmp(argv[1], "up") == 0) {
        int rc = tiku_ra8p1_sdram_attach();

        SHELL_PRINTF("sdram: attach rc=%d\n", rc);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "bench") == 0) {
        tiku_ra8p1_sdram_bench_run();
        return;
    }

    SHELL_PRINTF("sdram: %s, %lu MB at %lx\n",
                 tiku_ra8p1_sdram_ready() ? "up" : "down",
                 (unsigned long)(TIKU_RA8P1_SDRAM_BYTES >> 20),
                 (unsigned long)TIKU_RA8P1_SDRAM_ADDR);
    if (tiku_tier_stats(TIKU_MEM_PSRAM, &st) == TIKU_MEM_OK) {
        SHELL_PRINTF("  tier: %lu of %lu bytes used, peak %lu\n",
                     (unsigned long)st.used_bytes,
                     (unsigned long)st.total_bytes,
                     (unsigned long)st.peak_bytes);
    } else {
        SHELL_PRINTF("  tier: not attached (try `sdram up`)\n");
    }
    SHELL_PRINTF("Usage: sdram [up|bench]\n");
}

#endif /* TIKU_SHELL_CMD_SDRAM */
