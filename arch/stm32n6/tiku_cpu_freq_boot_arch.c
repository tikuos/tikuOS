/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - STM32N6 boot-time clock state.
 *
 * Starts HSI if it is not already running, and measures the inherited CPU
 * clock against LPTIM1 rather than asserting a number for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_stm32n6_regs.h"

/* Bounded so a dead oscillator surfaces as a fault rather than a hang. */
#define HSI_READY_SPINS     1000000UL

void tiku_cpu_boot_stm32n6_init(void) {
    TIKU_REG32(STM32N6_RCC_CR) |= STM32N6_RCC_CR_HSION;

    unsigned long spins = HSI_READY_SPINS;
    while ((TIKU_REG32(STM32N6_RCC_SR) & STM32N6_RCC_SR_HSIRDY) == 0UL) {
        if (--spins == 0UL) {
            return;
        }
    }
}

/** @brief CPU rate measured against LPTIM1; 0 until measured once. */
static unsigned long stm32n6_measured_hz;

/**
 * @brief Burn a fixed number of one-cycle spin iterations.
 *
 * The subs/bne pair retires at one iteration per cycle on this core, so the
 * iteration count stands in for a cycle count.
 *
 * @param iters  Iterations to run
 */
static void cpu_cal_spin(unsigned long iters) {
    __asm__ volatile (
        "1: subs %0, %0, #1\n"
        "   bne  1b\n"
        : "+r" (iters)
        :
        : "cc");
}

/**
 * @brief Measure the CPU clock against the LPTIM1 counter.
 *
 * Spins in fixed chunks until about 4 ms of LPTIM counts elapse, then converts
 * iterations per count into Hz. The window stays under one tick period so a
 * single counter wrap is unambiguous, and no interrupt needs to be running.
 *
 * @return Measured rate in Hz, or 0 when LPTIM1 is not running yet
 */
static unsigned long cpu_measure_hz(void) {
    /* CNTSTRT is a write trigger and does not read back, so "running" is
     * decided by whether the counter actually advances across a short spin. */
    if ((TIKU_REG32(STM32N6_LPTIM_CR(STM32N6_LPTIM1_BASE)) &
         STM32N6_LPTIM_CR_ENABLE) == 0UL) {
        return 0UL;
    }

    /* Two agreeing reads, since the counter lives in an asynchronous domain. */
    uint32_t start, check;
    do {
        start = TIKU_REG32(STM32N6_LPTIM_CNT(STM32N6_LPTIM1_BASE));
        check = TIKU_REG32(STM32N6_LPTIM_CNT(STM32N6_LPTIM1_BASE));
    } while (start != check);

    cpu_cal_spin(20000UL);      /* >= 30 us at any plausible core clock */
    uint32_t moved, again;
    do {
        moved = TIKU_REG32(STM32N6_LPTIM_CNT(STM32N6_LPTIM1_BASE));
        again = TIKU_REG32(STM32N6_LPTIM_CNT(STM32N6_LPTIM1_BASE));
    } while (moved != again);
    if (moved == start) {
        return 0UL;             /* enabled but not counting: nothing to measure */
    }

    /* 2000 counts at 500 kHz is 4 ms -- comfortably inside the 3906-count
     * reload period, so at most one wrap can occur. */
    const uint32_t want = 2000UL;
    const uint32_t chunk = 20000UL;
    unsigned long iters = 0UL;
    uint32_t elapsed = 0UL;

    while (elapsed < want) {
        cpu_cal_spin(chunk);
        iters += chunk;

        uint32_t now, again;
        do {
            now   = TIKU_REG32(STM32N6_LPTIM_CNT(STM32N6_LPTIM1_BASE));
            again = TIKU_REG32(STM32N6_LPTIM_CNT(STM32N6_LPTIM1_BASE));
        } while (now != again);

        elapsed = (now >= start) ? (now - start)
                                 : (now + TIKU_CLOCK_ARCH_INTERVAL - start);
        /* A boot-ROM clock so slow that one chunk exceeds a whole reload
         * period would alias; nothing measured has come within 50x of that. */
    }

    /* iters cycles over elapsed counts of a TIKU_STM32N6_LPTIM_HZ clock. */
    return (unsigned long)(((unsigned long long)iters * TIKU_STM32N6_LPTIM_HZ)
                           / elapsed);
}

unsigned long tiku_cpu_stm32n6_clock_get_hz(void) {
    if (stm32n6_measured_hz == 0UL) {
        stm32n6_measured_hz = cpu_measure_hz();
    }
    /* Before LPTIM1 runs there is nothing to measure against; report the
     * compile-time figure and try again on the next call. */
    return (stm32n6_measured_hz != 0UL) ? stm32n6_measured_hz
                                        : TIKU_STM32N6_CPU_HZ;
}

unsigned long tiku_cpu_stm32n6_smclk_get_hz(void) {
    uint32_t div = (TIKU_REG32(STM32N6_RCC_HSICFGR) & STM32N6_RCC_HSICFGR_DIV_MSK)
                   >> STM32N6_RCC_HSICFGR_DIV_POS;
    return STM32N6_HSI_HZ >> div;
}

int tiku_cpu_stm32n6_clock_has_fault(void) {
    return ((TIKU_REG32(STM32N6_RCC_SR) & STM32N6_RCC_SR_HSIRDY) == 0UL) ? 1 : 0;
}
