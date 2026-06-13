/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - RP2350 system tick (Cortex-M SysTick)
 *
 * Programs SysTick at TIKU_CLOCK_ARCH_SECOND Hz (default 128 Hz).
 * The ISR increments the tick counter, derives the seconds field,
 * and wakes the timer-poll process so software timers expire.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_timer_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_common.h"
#include <kernel/scheduler/tiku_sched.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Monotonic tick counter, incremented every SysTick interrupt. */
static volatile tiku_clock_arch_time_t g_ticks    = 0UL;
/** @brief Seconds counter, derived from g_ticks at TIKU_CLOCK_ARCH_SECOND. */
static volatile unsigned long          g_seconds  = 0UL;

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the RP2350 SysTick for the kernel clock.
 *
 * Resets g_ticks and g_seconds to zero (unlike the MSP430 port where
 * Timer A0 starts at zero automatically), then programs SysTick with
 * a reload value of TIKU_CLOCK_ARCH_INTERVAL - 1, clamped to the
 * 24-bit register width.  Enables the SysTick counter with the CPU
 * clock source and TICKINT asserted so every underflow fires the
 * tiku_rp2350_systick_handler() ISR.
 */
void tiku_clock_arch_init(void) {
    /* Reset the software tick / seconds accumulators. The MSP430 port
     * gets this for free because Timer A0 starts at zero, but here the
     * SysTick HW counter is independent of g_ticks — failing to reset
     * means tiku_clock_init() leaves the elapsed time intact across
     * re-init, which the test_clock_init_idempotent test fails on. */
    g_ticks   = 0UL;
    g_seconds = 0UL;

    /* SysTick reload value: CPU clock cycles per tick - 1. */
    uint32_t reload = (TIKU_CLOCK_ARCH_INTERVAL) - 1U;
    /* Clamp to the 24-bit reload register width. */
    if (reload > 0x00FFFFFFU) {
        reload = 0x00FFFFFFU;
    }
    _RP2350_REG(RP2350_SYST_RVR) = reload;
    _RP2350_REG(RP2350_SYST_CVR) = 0U;     /* clear current */
    _RP2350_REG(RP2350_SYST_CSR) =
        RP2350_SYST_CSR_ENABLE
        | RP2350_SYST_CSR_TICKINT
        | RP2350_SYST_CSR_CLKSRC_CPU;
}

/**
 * @brief Return the current tick count.
 *
 * @return Monotonic tick counter value (wraps at tiku_clock_arch_time_t max).
 */
tiku_clock_arch_time_t tiku_clock_arch_time(void) {
    return g_ticks;
}

/**
 * @brief Return the elapsed seconds since the last tiku_clock_arch_init().
 *
 * @return Seconds counter derived from the tick interrupt.
 */
unsigned long tiku_clock_arch_seconds(void) {
    return g_seconds;
}

/**
 * @brief Set the seconds counter (e.g. on RTC synchronization).
 *
 * @param sec  New seconds value to store in g_seconds.
 */
void tiku_clock_arch_set_seconds(unsigned long sec) {
    g_seconds = sec;
}

/**
 * @brief Spin-wait for @p t ticks.
 *
 * Busy-loops until g_ticks has advanced by at least @p t ticks from the
 * moment of the call.  Use sparingly; blocks the CPU without sleeping.
 *
 * @param t  Number of ticks to wait.
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t) {
    tiku_clock_arch_time_t target = g_ticks + t;
    while ((tiku_clock_arch_time_t)(target - g_ticks) > 0UL) {
        /* spin */
    }
}

/**
 * @brief Busy-delay for @p us microseconds using the CPU cycle counter.
 *
 * @param us  Delay duration in microseconds.
 */
void tiku_clock_arch_delay(unsigned int us) {
    tiku_cpu_rp2350_delay_us(us);
}

/**
 * @brief Return the sub-tick position within the current SysTick period.
 *
 * SysTick CVR counts down from RVR.  The fractional position is
 * expressed as a 16-bit value scaled over [0, 0xFFFF] so 0 = start
 * of tick and 0xFFFF = end.
 *
 * @return Sub-tick value in [0, 0xFFFF], or 0 if SysTick is not running.
 */
unsigned short tiku_clock_arch_fine(void) {
    /* SysTick CVR counts down from RVR. Express the position within
     * the current tick as a 16-bit value. */
    uint32_t cvr = _RP2350_REG(RP2350_SYST_CVR);
    uint32_t rvr = _RP2350_REG(RP2350_SYST_RVR);
    if (rvr == 0U) {
        return 0;
    }
    uint32_t fine = ((rvr - cvr) * 0xFFFFU) / rvr;
    return (unsigned short)fine;
}

/**
 * @brief Return the maximum value returned by tiku_clock_arch_fine().
 *
 * @return 0xFFFF on this port (16-bit sub-tick scale).
 */
int tiku_clock_arch_fine_max(void) {
    return 0xFFFF;
}

/**
 * @brief Report whether a clock fault has been detected.
 *
 * This port does not implement clock fault tracking; always returns 0.
 *
 * @return 0 (no fault tracking on RP2350).
 */
unsigned char tiku_clock_arch_fault(void) {
    return 0;   /* no clock fault tracking on this port */
}

/*---------------------------------------------------------------------------*/
/* SysTick ISR                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief SysTick interrupt handler — advances the kernel clock.
 *
 * Increments g_ticks on every SysTick underflow.  When g_ticks is a
 * multiple of TIKU_CLOCK_ARCH_SECOND, increments g_seconds.  Calls
 * tiku_sched_notify() to wake the timer-poll process so expired software
 * timers fire on the next scheduler iteration.
 */
void tiku_rp2350_systick_handler(void) {
    g_ticks++;
    if ((g_ticks % TIKU_CLOCK_ARCH_SECOND) == 0UL) {
        g_seconds++;
    }
    /* Wake the timer-poll process so any expired etimers fire on
     * the next scheduler iteration. */
    tiku_sched_notify();
}
