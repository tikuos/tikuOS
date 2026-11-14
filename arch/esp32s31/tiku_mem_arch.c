/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - S31 volatile memory tier attachment and secure wiping.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/memory/tiku_mem.h>
#include "tiku_mem_arch.h"
#include "tiku_psram_arch.h"
#include "tiku_runtime_arch.h"

int tiku_s31_memory_init(void)
{
    const uint8_t *base;
    tiku_mem_stats_t stats;
    uint32_t state;
    __asm__ volatile ("csrr %0,mstatus" : "=r"(state));
    if ((state & 8u) || tiku_s31_in_trap || !tiku_thread_in_kernel()) return -1;
    if (tiku_s31_psram_init() || tiku_tier_init() != TIKU_MEM_OK) return -1;
    if (tiku_tier_span_stats(TIKU_MEM_PSRAM, 0, &base, &stats) == TIKU_MEM_OK)
        return (uintptr_t)base == TIKU_S31_PSRAM_BASE &&
               stats.total_bytes == tiku_s31_psram_size() ? 0 : -1;
    return tiku_tier_attach_psram((void *)TIKU_S31_PSRAM_BASE,
                                 tiku_s31_psram_size()) == TIKU_MEM_OK ? 0 : -1;
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len)
{
    volatile uint8_t *p = buf;
    while (len--) *p++ = 0;
}
