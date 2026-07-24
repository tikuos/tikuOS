/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - RP2350 system-tick (Cortex-M SysTick)
 *
 * The system clock runs at TIKU_CLOCK_ARCH_SECOND ticks per second.
 * Default 128 Hz to match the MSP430 port — gives ~7.8 ms resolution
 * which is plenty for the protothread scheduler and far below the
 * 24-bit SysTick reload limit at 150 MHz CPU clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_TIMER_ARCH_H_
#define TIKU_RP2350_TIMER_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock tick counter type.
 *
 * Counts SysTick interrupts since boot.  On a 32-bit platform with
 * 128 Hz ticks this wraps in ~387 days — use wraparound-safe macros
 * (TIKU_CLOCK_LT / TIKU_CLOCK_GT) for comparisons.
 */
#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/** @brief Fine-resolution sub-tick counter type (SysTick CVR residue). */
typedef unsigned int tiku_clock_arch_counter_t;

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief System tick frequency in Hz (must be a power of 2).
 *
 * Default 128 Hz matches the MSP430 port (~7.8 ms per tick).
 * Override at compile time via -DTIKU_CLOCK_ARCH_CONF_SECOND=<n>.
 */
#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128   /* must be a power of 2 */
#endif

/** @brief Resolved tick frequency — use this in code, not the CONF_ form. */
#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND

/**
 * @brief SysTick reload value for one tick period at the current clk_sys.
 *
 * SysTick uses the CPU clock; reload = clk_sys / TICK_HZ.
 * TIKU_MAIN_CPU_HZ tracks MAIN_CPU_FREQ so the system tick stays at
 * TIKU_CLOCK_ARCH_SECOND Hz across all supported clk_sys frequencies
 * (12 / 48 / 100 / 125 / 133 / 150 MHz).
 */
#define TIKU_CLOCK_ARCH_INTERVAL  (TIKU_MAIN_CPU_HZ / TIKU_CLOCK_ARCH_SECOND)

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure SysTick for TIKU_CLOCK_ARCH_SECOND Hz and enable it.
 *
 * Called once from the kernel boot sequence.  Uses the processor clock
 * (CSR.CLKSRC = 1).
 */
void                   tiku_clock_arch_init(void);

/**
 * @brief Return the current tick counter value.
 *
 * Incremented by the SysTick ISR.  Wraparound-safe with the TIKU_CLOCK
 * arithmetic macros.
 *
 * @return Ticks since boot.
 */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/**
 * @brief Return the elapsed time in whole seconds since boot.
 *
 * @return Seconds since boot (tick_count / TIKU_CLOCK_ARCH_SECOND).
 */
unsigned long          tiku_clock_arch_seconds(void);

/**
 * @brief Overwrite the seconds counter (used by RTC sync).
 *
 * @param sec  New seconds value.
 */
void                   tiku_clock_arch_set_seconds(unsigned long sec);

/**
 * @brief Busy-wait until the tick counter reaches @p t.
 *
 * Uses wraparound-safe comparison; safe for delays up to half the
 * counter range (~193 days at 128 Hz).
 *
 * @param t  Target tick value (absolute, not a delta).
 */
void                   tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/**
 * @brief Busy-wait for at least @p us microseconds.
 *
 * Spins on the TIMER0 1 us hardware counter.  Does not yield to the
 * scheduler — use only for very short delays (< 1 tick, ~7.8 ms).
 *
 * @param us  Microseconds to wait.
 */
void                   tiku_clock_arch_delay(unsigned int us);

/**
 * @brief Read the sub-tick SysTick residue for fine-resolution timing.
 *
 * Returns the current SysTick CVR (count-value register) as a fraction
 * of the reload value, giving sub-tick resolution.  The value decrements
 * toward zero; compare against tiku_clock_arch_fine_max() for ordering.
 *
 * @return Sub-tick counter (0..fine_max).
 */
unsigned short         tiku_clock_arch_fine(void);

/**
 * @brief Return the maximum value of tiku_clock_arch_fine().
 *
 * Equals TIKU_CLOCK_ARCH_INTERVAL - 1 (the SysTick reload value minus
 * one, since CVR decrements from reload to 0).
 *
 * @return Maximum fine-counter value.
 */
int                    tiku_clock_arch_fine_max(void);

/**
 * @brief Convert milliseconds to tick counts.
 *
 * Integer arithmetic; result is rounded down.  Safe for ms values up
 * to ULONG_MAX / TIKU_CLOCK_ARCH_SECOND.
 */
#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    ((tiku_clock_arch_time_t)(((ms) * TIKU_CLOCK_ARCH_SECOND) / 1000))

/**
 * @brief Convert tick counts to milliseconds.
 *
 * Integer arithmetic; result is rounded down.
 */
#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    ((unsigned long)(((ticks) * 1000) / TIKU_CLOCK_ARCH_SECOND))

#endif /* TIKU_RP2350_TIMER_ARCH_H_ */
