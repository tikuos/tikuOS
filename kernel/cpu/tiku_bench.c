/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bench.c - portable micro-benchmark timebase.
 *
 * ARM prefers the DWT cycle counter with the kernel htimer as fallback; MSP430
 * uses Timer_A.  Nordic deliberately uses TIMER20 because its DWT counter freezes
 * without a live debugger, which would report plausible-looking zeros.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_bench.h"
#include <hal/tiku_cpu.h>
#include "tiku.h"
#include <kernel/timers/tiku_htimer.h>

#define TIKU_DEMCR_ADDR       0xE000EDFCUL
#define TIKU_DWT_CTRL_ADDR    0xE0001000UL
#define TIKU_DWT_CYCCNT_ADDR  0xE0001004UL
#define TIKU_DEMCR_TRCENA     (1UL << 24)
#define TIKU_DWT_CYCCNTENA    (1UL << 0)

enum tiku_bench_backend {
    TIKU_BENCH_HTIMER = 0,
    TIKU_BENCH_DWT = 1,
};

static uint8_t s_backend = TIKU_BENCH_HTIMER;

#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
static volatile uint32_t *const s_demcr =
    (volatile uint32_t *)(uintptr_t)TIKU_DEMCR_ADDR;
static volatile uint32_t *const s_dwt_ctrl =
    (volatile uint32_t *)(uintptr_t)TIKU_DWT_CTRL_ADDR;
static volatile uint32_t *const s_dwt_cyccnt =
    (volatile uint32_t *)(uintptr_t)TIKU_DWT_CYCCNT_ADDR;
#endif

void
tiku_bench_init(void)
{
    s_backend = TIKU_BENCH_HTIMER;
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
    *s_demcr |= TIKU_DEMCR_TRCENA;
    *s_dwt_cyccnt = 0u;
    *s_dwt_ctrl |= TIKU_DWT_CYCCNTENA;
    if ((*s_dwt_ctrl & TIKU_DWT_CYCCNTENA) != 0u) {
        uint32_t before = *s_dwt_cyccnt;
        __asm__ volatile ("nop\n\tnop\n\tnop\n\tnop" ::: "memory");
        if (*s_dwt_cyccnt != before) {
            s_backend = TIKU_BENCH_DWT;
        }
    }
#endif
}

tiku_bench_time_t
tiku_bench_now(void)
{
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
    if (s_backend == TIKU_BENCH_DWT) {
        return *s_dwt_cyccnt;
    }
#endif
    return (tiku_bench_time_t)tiku_htimer_arch_now();
}

uint32_t
tiku_bench_delta(tiku_bench_time_t start, tiku_bench_time_t end)
{
    if (s_backend == TIKU_BENCH_DWT) {
        return end - start;
    }
    return (uint32_t)(uint16_t)((uint16_t)end - (uint16_t)start);
}

const char *
tiku_bench_unit(void)
{
    return s_backend == TIKU_BENCH_DWT ? "cycles" : "ticks";
}

const char *
tiku_bench_clock(void)
{
    return s_backend == TIKU_BENCH_DWT ? "dwt" : "htimer";
}

uint32_t
tiku_bench_hz(void)
{
    if (s_backend != TIKU_BENCH_DWT) {
        return (uint32_t)TIKU_HTIMER_ARCH_SECOND;
    }
    /* The DWT counts core cycles, so its rate is whatever the core is
     * running at NOW -- not TIKU_MAIN_CPU_HZ, which is a compile-time
     * constant and stays at the boot frequency after any runtime change.
     * Reporting the constant scaled every converted duration on a board
     * running at 250 MHz by 2.6x. */
    {
        unsigned long hz = tiku_cpu_mclk_hz();
        return (hz != 0UL) ? (uint32_t)hz : (uint32_t)TIKU_MAIN_CPU_HZ;
    }
}

uint32_t
tiku_bench_resolution(void)
{
    return 1u;
}
