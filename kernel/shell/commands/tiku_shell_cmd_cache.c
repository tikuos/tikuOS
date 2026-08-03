/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cache.c - "cache" command (STM32N6).
 *
 * Shows and toggles the Cortex-M55 caches, and times a fixed workload so the
 * effect of a toggle is a number rather than an impression.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_cache.h"
#include <kernel/shell/tiku_shell.h>
#include <string.h>

#if defined(PLATFORM_STM32N6)

#include <arch/stm32n6/tiku_cache_arch.h>
#include <arch/stm32n6/tiku_dma_arch.h>
#include <arch/stm32n6/tiku_stm32n6_regs.h>
#include <hal/tiku_cpu.h>

/* The workload walks 256 KB of the tier arena: larger than either cache, so
 * the miss path is always exercised, and read-write so both allocate paths
 * are. The arena's front is free to scribble on before the tier hands it out,
 * but this deliberately uses its LAST portion, below anything allocated. */
extern uint32_t __axisram_end;
#define BENCH_WORDS  (65536U)

/** @brief DWT cycles for one pass of the read-modify-write walk. */
static uint32_t cache_bench_pass(void) {
    volatile uint32_t *buf =
        (volatile uint32_t *)((uintptr_t)&__axisram_end -
                              (BENCH_WORDS * sizeof(uint32_t)));

    TIKU_REG32(STM32N6_SCB_DEMCR)  |= STM32N6_SCB_DEMCR_TRCENA;
    TIKU_REG32(STM32N6_DWT_CTRL)   |= STM32N6_DWT_CTRL_CYCCNTENA;

    uint32_t start = TIKU_REG32(STM32N6_DWT_CYCCNT);
    for (uint32_t i = 0U; i < BENCH_WORDS; i++) {
        buf[i] = buf[i] + i;
    }
    return TIKU_REG32(STM32N6_DWT_CYCCNT) - start;
}

void tiku_shell_cmd_cache(uint8_t argc, const char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "on") == 0) {
        tiku_stm32n6_cache_enable();
    } else if (argc >= 2 && strcmp(argv[1], "off") == 0) {
        tiku_stm32n6_cache_disable();
    } else if (argc >= 2 && strcmp(argv[1], "bench") == 0) {
        /* Two passes: the first warms the cache (or proves there is none),
         * the second is the steady state a running system actually sees. */
        uint32_t cold = cache_bench_pass();
        uint32_t warm = cache_bench_pass();
        SHELL_PRINTF("  %u words r/m/w: cold %lu cycles, warm %lu"
                     " (%lu.%02lu/word)\n",
                     (unsigned)BENCH_WORDS,
                     (unsigned long)cold, (unsigned long)warm,
                     (unsigned long)(warm / BENCH_WORDS),
                     (unsigned long)((warm % BENCH_WORDS) * 100UL /
                                     BENCH_WORDS));
        return;
    } else if (argc >= 2 && strcmp(argv[1], "dma") == 0) {
        /* The coherency proof: the source is dirty in the cache when the
         * transfer starts and the destination stale after it finishes, so a
         * mismatch means a missing clean or invalidate, not a broken DMA. */
        volatile uint8_t *src =
            (volatile uint8_t *)((uintptr_t)&__axisram_end - 8192U);
        volatile uint8_t *dst =
            (volatile uint8_t *)((uintptr_t)&__axisram_end - 4096U);
        unsigned bad = 0U;

        for (unsigned i = 0U; i < 256U; i++) {
            src[i] = (uint8_t)(i ^ 0x5AU);
            dst[i] = 0U;
        }
        tiku_dma_arch_init();
        if (tiku_dma_arch_memcpy((void *)dst, (const void *)src, 256U,
                                 NULL, NULL) != 0) {
            SHELL_PRINTF("  dma: start failed\n");
            return;
        }
        while (tiku_dma_arch_busy()) {
        }
        for (unsigned i = 0U; i < 256U; i++) {
            if (dst[i] != (uint8_t)(i ^ 0x5AU)) {
                bad++;
            }
        }
        SHELL_PRINTF("  dma copied 256 bytes, %u mismatches%s\n", bad,
                     (bad == 0U) ? " (coherent)" : "");
        return;
    } else if (argc >= 2) {
        SHELL_PRINTF("Usage: cache [on | off | bench | dma]\n");
        return;
    }

    {
        uint32_t st = tiku_stm32n6_cache_state();
        SHELL_PRINTF("  I-cache %s, D-cache %s\n",
                     (st & 1U) ? "on" : "off",
                     (st & 2U) ? "on" : "off");
    }
}

#endif /* PLATFORM_STM32N6 */
