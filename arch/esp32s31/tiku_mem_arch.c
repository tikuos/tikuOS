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
#include "tiku_clock_arch.h"
#if TIKU_MEM_NVM_ENABLE
#include "tiku_storage_arch.h"
#include "tiku_mpu_arch.h"
#include "tiku_trng_arch.h"
#define PSRAM_RESERVED TIKU_S31_STORAGE_SHADOW
#else
#define PSRAM_RESERVED 0u
#endif

/* An image may place zero-initialized statics in a section that ends at the
 * top of PSRAM; the tier stops below it.  Undefined in other images. */
extern char __tiku_s31_psram_statics_start[] __attribute__((weak));

/** @brief Bytes at the top of PSRAM held by linker-placed statics, or -1 when
 *         the section lies outside the mapped PSRAM above the shadow. */
static long psram_statics(void)
{
    uintptr_t start = (uintptr_t)__tiku_s31_psram_statics_start;
    uintptr_t end = TIKU_S31_PSRAM_BASE + tiku_s31_psram_size();

    if (start == 0u) return 0;
    if (start < TIKU_S31_PSRAM_BASE + PSRAM_RESERVED || start > end) return -1;
    return (long)(end - start);
}

int tiku_s31_memory_init(void)
{
    const uint8_t *base;
    tiku_mem_stats_t stats;
    uint32_t state;
    __asm__ volatile ("csrr %0,mstatus" : "=r"(state));
    if ((state & 8u) || tiku_s31_in_trap || !tiku_thread_in_kernel()) return -1;
    if (tiku_s31_psram_init()) return -2;
#if TIKU_S31_CPU_MHZ != 40
    /* PSRAM's initialization calibrated the CPLL the boot rate runs from. */
    if (tiku_s31_clock_set(TIKU_S31_CPU_MHZ)) return -9;
#endif
#if TIKU_MEM_NVM_ENABLE
    if (tiku_s31_trng_init()) return -8;
    if (tiku_s31_durable_guard_off()) return -3;
    if (tiku_s31_storage_init()) return -4;
    tiku_mpu_init();
    if (!tiku_s31_mpu_ready()) return -5;
    tiku_persist_move_boot();
    if (tiku_persist_moved() < 0) return -6;
#endif
    if (tiku_tier_init() != TIKU_MEM_OK) return -7;
#if TIKU_MEM_NVM_ENABLE
    tiku_s31_storage_seal();
#endif
    long statics = psram_statics();
    if (statics < 0) return -10;
    if (tiku_tier_span_stats(TIKU_MEM_PSRAM, 0, &base, &stats) == TIKU_MEM_OK)
        return (uintptr_t)base == TIKU_S31_PSRAM_BASE + PSRAM_RESERVED &&
               stats.total_bytes == tiku_s31_psram_size() - PSRAM_RESERVED - (size_t)statics ? 0 : -1;
    return tiku_tier_attach_psram((void *)(TIKU_S31_PSRAM_BASE + PSRAM_RESERVED),
                                 tiku_s31_psram_size() - PSRAM_RESERVED - (size_t)statics) == TIKU_MEM_OK ? 0 : -1;
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len)
{
    volatile uint8_t *p = buf;
    while (len--) *p++ = 0;
}
