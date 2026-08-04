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
void tiku_ra8p1_htimer_on_tick(void);
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

int tiku_ra8p1_clock_arch_running(void)
{
    return (TIKU_REG32(RA8P1_SYST_CSR) &
            (RA8P1_SYST_CSR_ENABLE | RA8P1_SYST_CSR_TICKINT)) ==
           (RA8P1_SYST_CSR_ENABLE | RA8P1_SYST_CSR_TICKINT);
}

unsigned short tiku_clock_arch_fine(void)
{
    /* SysTick counts DOWN, so elapsed-within-tick is the complement. */
    uint32_t cvr = TIKU_REG32(RA8P1_SYST_CVR) & 0x00FFFFFFUL;
    return (unsigned short)(clock_reload - cvr);
}

unsigned long tiku_clock_arch_seconds(void)
{
    return (unsigned long)(clock_ticks / (tiku_clock_arch_time_t)
                           TIKU_CLOCK_ARCH_SECOND);
}

int tiku_clock_arch_fine_max(void)
{
    return (int)clock_reload;
}

void tiku_clock_arch_wait(tiku_clock_arch_time_t t)
{
    /* DURATION, not a deadline -- that is the kernel contract, and reading it
     * as absolute is a bug the Nordic port already made and recorded: any wait
     * shorter than the current uptime returns instantly, so every tick-paced
     * caller stops waiting once the system has been up a while.  Here it made
     * all five software-timer tests fail at once. */
    tiku_clock_arch_time_t target = clock_ticks + t;

    while ((long)(target - clock_ticks) > 0) {
        /* WFI, not a spin: only the tick ISR can end this wait. */
        __asm__ volatile ("wfi");
    }
}

void tiku_clock_arch_delay(unsigned int i)
{
    while (i-- != 0U) {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief SysTick exception: advance the tick and wake the scheduler.
 *
 * The notify call is not optional: without it expired timers never dispatch
 * and the failure looks like a dead console rather than a dead timer.
 */
void tiku_ra8p1_systick_handler(void)
{
    clock_ticks++;
#ifndef TIKU_MINIMAL
    /* The htimer has no compare hardware of its own, so this is where a due
     * alarm is noticed.  See tiku_htimer_arch.c for what that costs. */
    tiku_ra8p1_htimer_on_tick();
    tiku_sched_notify();
#endif
}
