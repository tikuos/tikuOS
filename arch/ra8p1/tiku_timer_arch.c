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
#include "tiku_cpu_freq_boot_arch.h"

#include <hal/tiku_clock_hal.h>

#ifndef TIKU_MINIMAL
#include <kernel/scheduler/tiku_sched.h>
#endif

/** @brief Monotonic tick counter, advanced by the SysTick exception. */
static volatile tiku_clock_arch_time_t clock_ticks;

/** @brief Reload currently programmed, so fine() can invert the down-counter. */
static uint32_t clock_reload = TIKU_CLOCK_ARCH_INTERVAL;

/**
 * @brief Right-shift applied to the sub-tick count.
 *
 * The HAL types fine() as unsigned short but the reload grows with the clock:
 * 62500 at 8 MHz fits, 1875000 at 240 MHz does not.  Shifting costs resolution
 * where truncating would cost correctness.
 */
static uint8_t clock_fine_shift;

/**
 * @brief Smallest shift that brings @p reload inside 16 bits.
 *
 * @param reload  SysTick reload value
 * @return Shift to apply to sub-tick counts
 */
static uint8_t fine_shift_for(uint32_t reload)
{
    uint8_t sh = 0U;

    while ((reload >> sh) > 0xFFFFUL && sh < 24U) {
        sh++;
    }
    return sh;
}

void tiku_clock_arch_init(void)
{
    /*
     * From the LIVE clock, not TIKU_CLOCK_ARCH_INTERVAL.  The boot constant is
     * an 8 MHz figure, and the kernel starts the tick AFTER the frequency
     * request -- so using it silently undid R4's retune and left the tick 30x
     * fast, with uptime racing and every timeout short by the same factor.
     */
    unsigned long hz = tiku_cpu_ra8p1_clock_get_hz();
    unsigned long reload = hz / (unsigned long)TIKU_CLOCK_ARCH_SECOND;

    if (reload == 0UL || reload > 0x01000000UL) {
        reload = TIKU_CLOCK_ARCH_INTERVAL;
    }

    clock_ticks = 0UL;
    clock_reload = (uint32_t)reload;
    clock_fine_shift = fine_shift_for(clock_reload);

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
    clock_fine_shift = fine_shift_for(clock_reload);
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
    return (unsigned short)((clock_reload - cvr) >> clock_fine_shift);
}

unsigned long tiku_clock_arch_seconds(void)
{
    return (unsigned long)(clock_ticks / (tiku_clock_arch_time_t)
                           TIKU_CLOCK_ARCH_SECOND);
}

int tiku_clock_arch_fine_max(void)
{
    return (int)(clock_reload >> clock_fine_shift);
}

unsigned char tiku_clock_arch_fault(void)
{
    /* The tick is SysTick off the processor clock, and the processor clock is
     * whatever the tree was switched to -- there is no lower-accuracy source
     * it can silently fall back to, so there is no fault of this KIND to
     * report.  A PLL that failed to lock leaves the tree on MOCO, which the
     * clock probe reports as its source; that is a different question and is
     * answered there rather than pretended to be answered here. */
    return TIKU_CLOCK_ARCH_FAULT_NONE;
}

uint32_t tiku_ra8p1_clock_arch_fine_hz(void)
{
    return (uint32_t)((unsigned long)TIKU_CLOCK_ARCH_SECOND *
                      (clock_reload >> clock_fine_shift));
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
        /* WFI, not a spin: only the tick ISR can end this wait.  Except
         * above 240 MHz, where entering Sleep at speed wrecks the machine
         * -- see tiku_cpu_boot_ra8p1_power_wfi_enter() for the story. */
        if (tiku_cpu_ra8p1_clock_get_hz() <= 240000000UL) {
            __asm__ volatile ("wfi");
        }
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
    tiku_sched_notify();
#endif
}
