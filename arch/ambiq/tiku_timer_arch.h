/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - Apollo510 system tick.
 *
 * The system clock runs at TIKU_CLOCK_ARCH_SECOND ticks per second.  The source is
 * the always-on STIMER, not SysTick, which freezes during WFI on this part -- see
 * tiku_timer_arch.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_TIMER_ARCH_H_
#define TIKU_AMBIQ_TIMER_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
/**
 * @brief Absolute system tick counter type.
 *
 * Holds the raw SysTick count since boot. unsigned long is at least
 * 32 bits on all supported targets; the wraparound period at 128 Hz
 * is ~388 days, well beyond any expected uptime.
 */
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/**
 * @brief Sub-tick fine counter type.
 *
 * Returned by tiku_clock_arch_fine() to provide sub-tick resolution.
 * Counts down from TIKU_CLOCK_ARCH_INTERVAL to 0 within each tick.
 */
typedef unsigned int tiku_clock_arch_counter_t;

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief System tick rate in ticks per second.
 *
 * Must be a power of 2. Defaults to 128 Hz, which balances timer
 * granularity (~7.8 ms) against interrupt overhead. Override at
 * compile time with TIKU_CLOCK_ARCH_CONF_SECOND.
 */
#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128
#endif

/** @brief Resolved tick rate — use this macro, not the _CONF_ version. */
#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND

/**
 * @brief SysTick reload value.
 *
 * SysTick is clocked from the core clock, so reload = core_hz / TICK_HZ, and
 * TIKU_MAIN_CPU_HZ tracks MAIN_CPU_FREQ so the tick stays accurate across a
 * frequency change.  At 96 MHz and 128 Hz this is 750000 (24-bit limit).
 */
#define TIKU_CLOCK_ARCH_INTERVAL  (TIKU_MAIN_CPU_HZ / TIKU_CLOCK_ARCH_SECOND)

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize SysTick and start the system tick interrupt.
 *
 * Programs the SysTick reload register to TIKU_CLOCK_ARCH_INTERVAL,
 * enables the SysTick exception, and starts the counter. Called once
 * during boot from tiku_clock_init().
 */
void                   tiku_clock_arch_init(void);

/**
 * @brief Return the current absolute tick counter.
 *
 * Incremented by one in the SysTick ISR. Safe to read from any
 * context; the read is atomic on Cortex-M (word-aligned load).
 *
 * @return Current system tick count since boot.
 */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/**
 * @brief Return the elapsed time in whole seconds since boot.
 *
 * Derived from the tick counter: seconds = ticks / TIKU_CLOCK_ARCH_SECOND.
 *
 * @return Elapsed seconds since boot.
 */
unsigned long          tiku_clock_arch_seconds(void);

/**
 * @brief Set the seconds counter (e.g. after RTC synchronisation).
 *
 * Adjusts the internal tick counter so that tiku_clock_arch_seconds()
 * returns @p sec on the next call.
 *
 * @param sec  New seconds value to apply.
 */
void                   tiku_clock_arch_set_seconds(unsigned long sec);

/**
 * @brief Block until the system tick counter reaches @p t.
 *
 * Busy-waits; for long delays prefer tiku_timer event-driven patterns.
 *
 * @param t  Target tick count to wait for.
 */
void                   tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/**
 * @brief Busy-wait for approximately @p us microseconds.
 *
 * Uses a calibrated loop or the SysTick fine counter. Not suitable
 * for precision timing; use tiku_htimer for accurate delays.
 *
 * @param us  Delay in microseconds.
 */
void                   tiku_clock_arch_delay(unsigned int us);

/**
 * @brief Read the sub-tick SysTick down-counter.
 *
 * Returns the current SysTick VAL register, which counts from
 * TIKU_CLOCK_ARCH_INTERVAL down to 0 within each tick. Useful for
 * measuring intervals shorter than one tick.
 *
 * @return Current SysTick VAL (counts down; 0 = tick boundary).
 */
unsigned short         tiku_clock_arch_fine(void);

/**
 * @brief Return the maximum value of the fine counter.
 *
 * Equal to TIKU_CLOCK_ARCH_INTERVAL - 1. Used by callers that need
 * to normalise the sub-tick counter to a fraction.
 *
 * @return Maximum fine counter value.
 */
int                    tiku_clock_arch_fine_max(void);

/**
 * @brief Convert milliseconds to system ticks.
 *
 * @param ms  Time in milliseconds.
 * @return Equivalent number of ticks (rounded down).
 */
#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    ((tiku_clock_arch_time_t)(((ms) * TIKU_CLOCK_ARCH_SECOND) / 1000))

/**
 * @brief Convert system ticks to milliseconds.
 *
 * @param ticks  Number of system ticks.
 * @return Equivalent time in milliseconds (rounded down).
 */
#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    ((unsigned long)(((ticks) * 1000) / TIKU_CLOCK_ARCH_SECOND))

/*---------------------------------------------------------------------------*/
/* STIMER TIMEBASE RECLOCK (tiku_htimer_arch.c) -- deep-sleep support        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Switch the STIMER timebase: XTAL 32.768 kHz <-> LFRC ~900 Hz.
 *
 * The crystal dies under real (debugger-free) deep sleep on this port, freezing
 * the STIMER and turning a tickless stretch into a sleep with no alarm, so the
 * deep path reclocks to the LFRC around the sleep window.
 *
 * @note Verified switch (reverts to XTAL on a dead source); tick accounting
 *       stays continuous; refuses while a tickless stretch is open.
 * @param use_lfrc  non-zero: to LFRC (rate measured against DWT); 0: to XTAL
 * @return new rate in Hz, or 0 on failure (timebase left on XTAL)
 */
uint32_t tiku_ambiq_stimer_reclock(int use_lfrc);

/** @brief Current STIMER timebase rate in Hz (32768 on XTAL). */
uint32_t tiku_ambiq_stimer_rate_hz(void);

#endif /* TIKU_AMBIQ_TIMER_ARCH_H_ */
