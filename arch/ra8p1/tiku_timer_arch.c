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
#include "tiku_cpu_common.h"

#include <hal/tiku_clock_hal.h>

#ifndef TIKU_MINIMAL
#include <kernel/scheduler/tiku_sched.h>
#include "tiku_cpu_watchdog_arch.h"
#endif

/** @brief Monotonic tick counter, advanced by the SysTick exception. */
static volatile tiku_clock_arch_time_t clock_ticks;

/** @brief Reload in SysTick, which fine() uses to invert the down-counter. */
static uint32_t clock_reload = TIKU_CLOCK_ARCH_INTERVAL;

/**
 * @brief Right-shift applied to the sub-tick count.
 *
 * The HAL types fine() as unsigned short, and the reload grows with the
 * clock: 62500 at 8 MHz fits in 16 bits, 1875000 at 240 MHz does not.  The
 * shift keeps the count in range at reduced resolution.
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
     * The reload comes from the live clock, since the kernel starts the tick
     * after the frequency request.  TIKU_CLOCK_ARCH_INTERVAL, the 8 MHz boot
     * figure, is used only when the live clock gives no valid reload.
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

    /* SysTick's reload is 24 bits; a reload that does not fit is refused and
     * the tick is left as it was. */
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
    /* SysTick counts down, so the elapsed count is the reload minus CVR. */
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
    /* The tick runs on the processor clock, which has no lower-accuracy
     * source to fall back to, so there is no fault to report.  A PLL that
     * fails to lock leaves the tree on MOCO, which the clock probe reports as
     * its source. */
    return TIKU_CLOCK_ARCH_FAULT_NONE;
}

uint32_t tiku_ra8p1_clock_arch_fine_hz(void)
{
    return (uint32_t)((unsigned long)TIKU_CLOCK_ARCH_SECOND *
                      (clock_reload >> clock_fine_shift));
}

void tiku_clock_arch_wait(tiku_clock_arch_time_t t)
{
    /* t is a duration in ticks, as the kernel contract defines it. */
    tiku_clock_arch_time_t target = clock_ticks + t;
    uint32_t primask;

    /*
     * With PRIMASK set the tick ISR cannot run and the counter never moves,
     * though a pending SysTick still ends each WFI.  Early boot holds
     * interrupts off, so with PRIMASK set or the tick stopped the wait is a
     * calibrated spin.
     */
    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    if (primask != 0UL || !tiku_ra8p1_clock_arch_running()) {
        while (t-- != 0U) {
            tiku_cpu_ra8p1_delay_us(1000000UL /
                                    (unsigned long)TIKU_CLOCK_ARCH_SECOND);
        }
        return;
    }

    while ((long)(target - clock_ticks) > 0) {
        /* Only the tick ISR advances the counter, so the core sleeps between
         * ticks, through the arch entry point: above 240 MHz, sleeping needs
         * the divider step-down done there. */
        tiku_cpu_boot_ra8p1_power_wfi_enter();
    }
}

void tiku_clock_arch_delay(unsigned int i)
{
    while (i-- != 0U) {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief SysTick exception: advance the tick, feed an IWDT the caller has
 *        turned off, and wake the scheduler.
 *
 * Expired timers dispatch only after tiku_sched_notify().
 */
void tiku_ra8p1_systick_handler(void)
{
    clock_ticks++;
#ifndef TIKU_MINIMAL
    tiku_cpu_ra8p1_watchdog_tick_arch();
    tiku_sched_notify();
#endif
}
