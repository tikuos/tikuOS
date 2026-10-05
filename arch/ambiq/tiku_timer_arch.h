/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - Ambiq system tick (Apollo4 Lite/4P and Apollo510).
 *
 * The system clock runs at TIKU_CLOCK_ARCH_SECOND ticks per second from the
 * always-on STIMER, since SysTick stops during WFI on these parts.  SysTick
 * free-runs without an interrupt as the busy-delay counter.
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
 * Holds the STIMER tick count since boot.  At 128 Hz the 32-bit count wraps
 * after about 388 days.
 */
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/** @brief Sub-tick fine counter type; this port has no sub-tick counter. */
typedef unsigned int tiku_clock_arch_counter_t;

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief System tick rate in ticks per second; 128 (7.8 ms) by default.
 *
 * Must be a power of 2, so that a tick is a whole number of 32.768 kHz
 * STIMER counts.  Override at compile time.
 */
#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128
#endif

/** @brief Resolved tick rate — use this macro, not the _CONF_ version. */
#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND

/**
 * @brief SysTick period in core clock cycles (RVR holds this minus 1).
 *
 * SysTick free-runs on this period as the busy-delay counter and raises no
 * interrupt.  At 96 MHz and 128 Hz it is 750000, inside the 24-bit field.
 */
#define TIKU_CLOCK_ARCH_INTERVAL  (TIKU_MAIN_CPU_HZ / TIKU_CLOCK_ARCH_SECOND)

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Start the system tick.
 *
 * Sets SysTick free-running without an interrupt, then starts the STIMER
 * periodic tick at TIKU_CLOCK_ARCH_SECOND Hz.
 *
 * @note Called once during boot from tiku_clock_init().
 */
void                   tiku_clock_arch_init(void);

/**
 * @brief Return the current absolute tick counter.
 *
 * The STIMER tick interrupt advances it by one, and the end of a tickless
 * stretch by the whole ticks slept.  The read is one aligned word load.
 *
 * @return Current system tick count since boot.
 */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/**
 * @brief Return the seconds counter.
 *
 * Kept apart from the tick counter: it advances once per
 * TIKU_CLOCK_ARCH_SECOND ticks, from boot or from the last set value.
 *
 * @return The value tiku_clock_arch_set_seconds() last loaded (0 from boot)
 *         plus the whole seconds counted since.
 */
unsigned long          tiku_clock_arch_seconds(void);

/**
 * @brief Set the seconds counter (e.g. after RTC synchronisation).
 *
 * The tick counter is unchanged.
 *
 * @param sec  New seconds value to apply.
 */
void                   tiku_clock_arch_set_seconds(unsigned long sec);

/**
 * @brief Busy-wait until the tick counter has advanced by @p t ticks.
 *
 * @note Interrupts must be enabled: the tick interrupt advances the counter
 *       this polls.
 * @param t  Number of ticks to wait.
 */
void                   tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/**
 * @brief Busy-wait for @p us microseconds.
 *
 * Counts SysTick cycles at the live core clock through
 * tiku_cpu_ambiq_delay_us().
 *
 * @param us  Delay in microseconds.
 */
void                   tiku_clock_arch_delay(unsigned int us);

/**
 * @brief Read the sub-tick fine counter.
 *
 * @return 0 always: this port has no sub-tick counter.
 */
unsigned short         tiku_clock_arch_fine(void);

/**
 * @brief Return the maximum value of the fine counter.
 *
 * @return 1 always; a non-zero value keeps a caller's normalisation of
 *         tiku_clock_arch_fine() defined.
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
/* STIMER TIMEBASE RECLOCK (Apollo510, tiku_htimer_arch.c)                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Switch the STIMER timebase: XTAL 32.768 kHz <-> LFRC ~900 Hz.
 *
 * Debugger-free deep sleep stops the crystal and the STIMER with it, leaving
 * a tickless stretch with no alarm; the LFRC keeps running.
 *
 * @note Refuses while a tickless stretch is open.  Ticks elapsed so far are
 *       credited at the old rate, then the tick is re-armed at the new one.
 * @param use_lfrc  non-zero: to LFRC (rate measured against DWT); 0: to XTAL
 * @return The new rate in Hz; 0 if a stretch is open (timebase unchanged) or
 *         the new source did not count (timebase back on XTAL)
 */
uint32_t tiku_ambiq_stimer_reclock(int use_lfrc);

/** @brief Current STIMER timebase rate in Hz (32768 on XTAL). */
uint32_t tiku_ambiq_stimer_rate_hz(void);

#endif /* TIKU_AMBIQ_TIMER_ARCH_H_ */
