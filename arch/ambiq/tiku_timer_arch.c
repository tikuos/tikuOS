/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - Apollo 510 system tick (Cortex-M SysTick)
 *
 * Drives the system clock at TIKU_CLOCK_ARCH_SECOND Hz from the core
 * clock. SysTick is a Cortex-M core peripheral (same SCS registers on
 * M55 as M33), so this is bare-metal — no AmbiqSuite dependency except
 * the coarse busy-delay helper.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "tiku_timer_arch.h"
#include "tiku_cpu_common.h"   /* tiku_cpu_ambiq_delay_us (bare-metal, calibrated) */
#include "kernel/scheduler/tiku_sched.h"

/**
 * @defgroup SYST Cortex-M SysTick registers (System Control Space)
 * @brief Direct-mapped SysTick CSR/RVR/CVR registers and control bits.
 *
 * These address the SysTick core peripheral at 0xE000E010. Identical
 * on every Cortex-M variant (M0/M3/M33/M55), so no AmbiqSuite dependency
 * is needed for the timer implementation.
 * @{
 */
#define SYST_CSR  (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR  (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR  (*(volatile uint32_t *)0xE000E018UL)
#define SYST_CSR_ENABLE     (1u << 0)
#define SYST_CSR_TICKINT    (1u << 1)
#define SYST_CSR_CLKSOURCE  (1u << 2)   /* processor clock */
/** @} */

/** @brief Monotonic tick counter incremented by each SysTick interrupt. */
static volatile unsigned long  s_ticks   = 0;

/** @brief Whole-second counter derived from the sub-second divider. */
static volatile unsigned long  s_seconds = 0;

/**
 * @brief Sub-second tick accumulator; wraps at TIKU_CLOCK_ARCH_SECOND.
 */
static volatile unsigned int   s_subsec  = 0;

/**
 * @brief Initialize the SysTick peripheral for the system clock
 *
 * Programs SysTick to fire every TIKU_CLOCK_ARCH_INTERVAL processor
 * clock cycles (derived from TIKU_CLOCK_ARCH_SECOND), using the core
 * clock source (no external reference). Enables the SysTick interrupt
 * so tiku_ambiq_systick_handler() is invoked on each reload.
 */
void tiku_clock_arch_init(void) {
    SYST_RVR = (uint32_t)(TIKU_CLOCK_ARCH_INTERVAL - 1u);
    SYST_CVR = 0u;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

/**
 * @brief SysTick exception handler — advances the system clock
 *
 * Increments the tick counter and the sub-second accumulator on every
 * reload. When the accumulator reaches TIKU_CLOCK_ARCH_SECOND it rolls
 * over and bumps the whole-second counter. Calls tiku_sched_notify()
 * to wake the scheduler after each tick.
 *
 * Installed at vector slot 15 in tiku_crt_early.c.
 */
void tiku_ambiq_systick_handler(void) {
    s_ticks++;
    if (++s_subsec >= TIKU_CLOCK_ARCH_SECOND) {
        s_subsec = 0;
        s_seconds++;
    }
    tiku_sched_notify();
}

/**
 * @brief Return the current tick count
 *
 * @return Monotonically increasing tick counter value
 */
tiku_clock_arch_time_t tiku_clock_arch_time(void) {
    return (tiku_clock_arch_time_t)s_ticks;
}

/**
 * @brief Return the elapsed whole-second counter
 *
 * @return Seconds elapsed since tiku_clock_arch_init()
 */
unsigned long tiku_clock_arch_seconds(void)        { return s_seconds; }

/**
 * @brief Set the whole-second counter (RTC epoch synchronisation)
 *
 * @param sec  New seconds value to load
 */
void          tiku_clock_arch_set_seconds(unsigned long sec) { s_seconds = sec; }

/**
 * @brief Spin-wait for a number of system ticks
 *
 * Busy-loops until s_ticks has advanced by at least t ticks. Uses
 * signed subtraction for correct wraparound handling.
 *
 * @param t  Number of ticks to wait
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t) {
    tiku_clock_arch_time_t target = (tiku_clock_arch_time_t)s_ticks + t;
    while ((long)(target - (tiku_clock_arch_time_t)s_ticks) > 0) {
        /* spin — relies on SysTick advancing s_ticks */
    }
}

/**
 * @brief Busy-delay for a given number of microseconds
 *
 * Delegates to the bare-metal calibrated DWT loop in tiku_cpu_common.
 *
 * @param us  Delay duration in microseconds
 */
void tiku_clock_arch_delay(unsigned int us) {
    tiku_cpu_ambiq_delay_us(us);   /* bare-metal (calibrated DWT) */
}

/**
 * @brief Return the sub-tick fine counter (not yet modelled)
 *
 * Sub-tick resolution is not implemented on this port. Returns 0;
 * the maximum is reported as 1 to avoid division by zero in callers.
 *
 * @return 0 (coarse placeholder)
 */
unsigned short tiku_clock_arch_fine(void)     { return 0; }

/**
 * @brief Return the maximum value of the fine counter
 *
 * @return 1 (safe non-zero placeholder; sub-tick not modelled)
 */
int            tiku_clock_arch_fine_max(void) { return 1; }

/**
 * @brief Report whether the last tick had a clock fault
 *
 * @return 0 always (fault detection not implemented on this port)
 */
unsigned char  tiku_clock_arch_fault(void)    { return 0; }
