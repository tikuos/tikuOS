/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - RA8P1 kernel clock on SysTick.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_timer_arch.h"
#include "tiku_ra8p1_regs.h"

#ifndef TIKU_MINIMAL
#include <kernel/scheduler/tiku_sched.h>
#endif

/** @brief Monotonic tick counter, advanced by the SysTick exception. */
static volatile tiku_clock_arch_time_t clock_ticks;

/** @brief Reload currently programmed, so fine() can invert the down-counter. */
static uint32_t clock_reload = TIKU_CLOCK_ARCH_INTERVAL;

void tiku_clock_arch_init(void)
{
    clock_ticks = 0UL;
    clock_reload = TIKU_CLOCK_ARCH_INTERVAL;

    TIKU_REG32(RA8P1_SYST_CSR) = 0UL;               /* stop before re-arming */
    TIKU_REG32(RA8P1_SYST_RVR) = clock_reload - 1UL;
    TIKU_REG32(RA8P1_SYST_CVR) = 0UL;               /* any write clears it   */
    TIKU_REG32(RA8P1_SYST_CSR) = RA8P1_SYST_CSR_CLKSOURCE |
                                 RA8P1_SYST_CSR_TICKINT |
                                 RA8P1_SYST_CSR_ENABLE;
}

int tiku_ra8p1_clock_arch_retune(unsigned long iclk_hz)
{
    unsigned long reload = iclk_hz / (unsigned long)TIKU_CLOCK_ARCH_SECOND;

    /* SysTick's reload is 24 bits.  Refusing is the honest outcome: a silent
     * truncation would leave a tick running at some unrelated rate, and every
     * timeout in the system would be quietly wrong. */
    if (reload == 0UL || reload > 0x01000000UL) {
        return -1;
    }

    TIKU_REG32(RA8P1_SYST_CSR) = 0UL;
    clock_reload = (uint32_t)reload;
    TIKU_REG32(RA8P1_SYST_RVR) = clock_reload - 1UL;
    TIKU_REG32(RA8P1_SYST_CVR) = 0UL;
    TIKU_REG32(RA8P1_SYST_CSR) = RA8P1_SYST_CSR_CLKSOURCE |
                                 RA8P1_SYST_CSR_TICKINT |
                                 RA8P1_SYST_CSR_ENABLE;
    return 0;
}

tiku_clock_arch_time_t tiku_clock_arch_time(void)
{
    return clock_ticks;
}

tiku_clock_arch_counter_t tiku_clock_arch_fine(void)
{
    /* SysTick counts DOWN, so elapsed-within-tick is the complement. */
    uint32_t cvr = TIKU_REG32(RA8P1_SYST_CVR) & 0x00FFFFFFUL;
    return (tiku_clock_arch_counter_t)(clock_reload - cvr);
}

/**
 * @brief SysTick exception: advance the tick and wake the scheduler.
 *
 * The notify call is not optional.  Without it expired timers are never
 * dispatched and a shell never polls its input -- the failure looks like a
 * dead console rather than a dead timer, which is how the STM32N6 port lost
 * an hour to it.
 */
void tiku_ra8p1_systick_handler(void)
{
    clock_ticks++;
#ifndef TIKU_MINIMAL
    tiku_sched_notify();
#endif
}
