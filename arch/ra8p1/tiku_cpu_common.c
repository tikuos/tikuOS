/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - RA8P1 busy-wait delays.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"

/**
 * @brief Spin for a given number of loop iterations.
 *
 * @param iters  Iterations to run; zero still costs one pass
 */
static void cpu_spin(unsigned long iters)
{
    if (iters == 0UL) { iters = 1UL; }
    __asm__ volatile (
        "1: subs %0, %0, #1\n"
        "   bne  1b\n"
        : "+r" (iters)
        :
        : "cc");
}

void tiku_cpu_ra8p1_delay_us(unsigned int us)
{
    unsigned long per_ms = tiku_cpu_ra8p1_spin_per_ms();

    /* Split so the multiply cannot overflow on long waits. */
    while (us >= 1000U) {
        cpu_spin(per_ms);
        us -= 1000U;
    }
    cpu_spin(((unsigned long)us * per_ms) / 1000UL);
}

void tiku_cpu_ra8p1_delay_ms(unsigned int ms)
{
    unsigned long per_ms = tiku_cpu_ra8p1_spin_per_ms();

    while (ms-- > 0U) {
        cpu_spin(per_ms);
    }
}
