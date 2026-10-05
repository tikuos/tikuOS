/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - RP2350 system tick (Cortex-M SysTick).
 *
 * Runs at TIKU_CLOCK_ARCH_SECOND ticks per second, 128 Hz (7.8 ms) by
 * default.  At 150 MHz one tick is 1171875 cycles, inside the 24-bit SysTick
 * reload.
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
 * Counts SysTick interrupts since tiku_clock_arch_init().  At 32 bits and
 * 128 Hz it wraps after about 388 days; compare ticks with the
 * wraparound-safe TIKU_CLOCK_LT and TIKU_CLOCK_DIFF (tiku_clock.h).
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
 * Default 128 Hz, 7.8 ms per tick.  Override at compile time with
 * -DTIKU_CLOCK_ARCH_CONF_SECOND=<n>.
 */
#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128
#endif

/** @brief Resolved tick frequency; code uses this, not the CONF_ form. */
#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND

/**
 * @brief CPU cycles per tick at the configured MAIN_CPU_FREQ.
 *
 * TIKU_MAIN_CPU_HZ / TIKU_CLOCK_ARCH_SECOND.  tiku_clock_arch_init() programs
 * SysTick from the measured clk_sys, not from this macro.
 */
#define TIKU_CLOCK_ARCH_INTERVAL  (TIKU_MAIN_CPU_HZ / TIKU_CLOCK_ARCH_SECOND)

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure SysTick for TIKU_CLOCK_ARCH_SECOND Hz and enable it.
 *
 * Uses the processor clock (CSR.CLKSRC = 1) and zeroes the tick and seconds
 * counters.
 *
 * @note Called by tiku_clock_init() during boot.
 */
void                   tiku_clock_arch_init(void);

/**
 * @brief Return the current tick counter value.
 *
 * Incremented by the SysTick ISR.  Wraparound-safe with the TIKU_CLOCK
 * arithmetic macros.
 *
 * @return Ticks since tiku_clock_arch_init().
 */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/**
 * @brief Return the elapsed time in whole seconds since tiku_clock_arch_init().
 *
 * @return Seconds counter, which the tick ISR advances every
 *         TIKU_CLOCK_ARCH_SECOND ticks.
 */
unsigned long          tiku_clock_arch_seconds(void);

/**
 * @brief Overwrite the seconds counter (used by RTC sync).
 *
 * @param sec  New seconds value.
 */
void                   tiku_clock_arch_set_seconds(unsigned long sec);

/**
 * @brief Busy-wait for @p t clock ticks.
 *
 * Spins until the tick counter has advanced by @p t from its value at the
 * call; works across a counter wrap.
 *
 * @param t  Number of ticks to wait (a delta).
 */
void                   tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/**
 * @brief Busy-wait for at least @p us microseconds.
 *
 * Spins on the TIMER0 1 us hardware counter.  Does not yield to the
 * scheduler.
 *
 * @param us  Microseconds to wait.
 */
void                   tiku_clock_arch_delay(unsigned int us);

/**
 * @brief Read the position within the current tick for fine timing.
 *
 * Scales the SysTick count (RVR - CVR) to 0..0xFFFF across the tick period:
 * 0 at the start of a tick, rising toward 0xFFFF at its end.
 *
 * @return Sub-tick position (0..tiku_clock_arch_fine_max()).
 */
unsigned short         tiku_clock_arch_fine(void);

/**
 * @brief Return the maximum value of tiku_clock_arch_fine().
 *
 * @return 0xFFFF.
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
