/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock.h - System clock interface
 *
 * Provides tick counting, time queries, and delay functions.
 * Delegates to architecture-specific implementations via the HAL.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CLOCK_H_
#define TIKU_CLOCK_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <hal/tiku_clock_hal.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @typedef tiku_clock_time_t
 * @brief System clock time type
 *
 * 16 bits on MSP430 and unsigned long elsewhere; defining
 * TIKU_CLOCK_CONF_TIME_T before including this header overrides it.
 */
#ifdef TIKU_CLOCK_CONF_TIME_T
typedef TIKU_CLOCK_CONF_TIME_T tiku_clock_time_t;
#elif defined(PLATFORM_MSP430)
typedef unsigned short tiku_clock_time_t;
#else
/* These defaults must match the width tiku.h configures.  A translation unit
 * can reach this header before tiku.h, and the include guard keeps the first
 * typedef: a narrower one makes its struct tiku_timer 4 bytes shorter than the
 * timer subsystem's, and the timer's stores overrun the caller's object. */
typedef unsigned long tiku_clock_time_t;
#endif

/**
 * @brief Longest interval the clock type can measure: half its range.
 *
 * The counter wraps; the arithmetic below is correct only for intervals
 * shorter than this (256 s at 16 bits and 128 Hz).  A longer span is a sum of
 * short differences, each in the counter's own width.
 */
#define TIKU_CLOCK_MAX_INTERVAL \
    ((tiku_clock_time_t)(((tiku_clock_time_t)~(tiku_clock_time_t)0) / 2u))

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_CLOCK_SECOND
 * @brief Number of clock ticks per second
 */
#define TIKU_CLOCK_SECOND TIKU_CLOCK_ARCH_SECOND

/*---------------------------------------------------------------------------*/
/* CLOCK ARITHMETIC                                                          */
/*---------------------------------------------------------------------------*/

/*
 * Both use the clock type's own width, so they stay correct when
 * TIKU_CLOCK_CONF_TIME_T overrides it.
 */

/**
 * @def TIKU_CLOCK_LT(a, b)
 * @brief Non-zero when @p a is before @p b, across a counter wrap, for
 *        points less than TIKU_CLOCK_MAX_INTERVAL apart.
 */
#define TIKU_CLOCK_LT(a, b) \
    ((tiku_clock_time_t)((a) - (b)) > TIKU_CLOCK_MAX_INTERVAL)

/**
 * @def TIKU_CLOCK_DIFF(a, b)
 * @brief Signed difference a - b as a long, across a counter wrap, for points
 *        less than TIKU_CLOCK_MAX_INTERVAL apart.
 */
#define TIKU_CLOCK_DIFF(a, b)                                                 \
    (TIKU_CLOCK_LT((a), (b)) ? -(long)(tiku_clock_time_t)((b) - (a))          \
                             :  (long)(tiku_clock_time_t)((a) - (b)))

/**
 * @def TIKU_CLOCK_MS_TO_TICKS(ms)
 * @brief Convert milliseconds to clock ticks, rounding down.
 *
 * The product (ms) * TIKU_CLOCK_SECOND is computed in the type of @p ms: on
 * MSP430 an int argument above 255 overflows, so pass an unsigned long.
 */
#define TIKU_CLOCK_MS_TO_TICKS(ms) \
    ((tiku_clock_time_t)(((ms) * TIKU_CLOCK_SECOND) / 1000))

/*---------------------------------------------------------------------------*/
/* CORE API                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the system clock
 *
 * Delegates to tiku_clock_arch_init().
 *
 * @note Call once during system boot.
 */
void tiku_clock_init(void);

/**
 * @brief Get current clock time in ticks
 * @return Current tick count
 */
tiku_clock_time_t tiku_clock_time(void);

/**
 * @brief Get current time in seconds
 * @return Seconds since system start
 */
unsigned long tiku_clock_seconds(void);

/**
 * @brief Busy-wait for specified clock ticks
 * @param t Number of ticks to wait
 */
void tiku_clock_wait(tiku_clock_time_t t);

/**
 * @brief CPU delay in microsecond-scale units
 * @param dt Delay units (platform-specific calibration)
 */
void tiku_clock_delay_usec(unsigned int dt);

/**
 * @brief Return the active clock-source fault code.
 *
 * Non-zero when the platform fell back from the configured low-frequency
 * source, in which case the tick rate differs from TIKU_CLOCK_SECOND and every
 * software timer expires at a proportionally different wall-clock rate.
 */
unsigned char tiku_clock_fault(void);

/*---------------------------------------------------------------------------*/
/* TICKLESS IDLE (optional per-arch backend)                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Stretch the next tick interrupt up to @p ticks_ahead away.
 *
 * The scheduler calls it before a tick-woken idle while timers are armed but
 * none due.  The arch may program its compare straight to the next deadline,
 * and counts the elapsed ticks on wake, so the kernel clock stays exact.
 *
 * @param ticks_ahead Ticks until the next software-timer deadline (>1)
 * @return Non-zero if the stretch was armed, 0 if unsupported
 * @note Call with interrupts masked.
 */
int tiku_clock_tickless_begin(tiku_clock_time_t ticks_ahead);

/**
 * @brief Close a stretch window opened by tiku_clock_tickless_begin().
 *
 * Called with interrupts masked after the idle hook returns.  A no-op if the
 * stretched compare already fired; on an early wake it accounts the elapsed
 * whole ticks and re-arms the per-tick cadence at the next boundary.
 */
void tiku_clock_tickless_end(void);

/**
 * @brief Does this build have a tickless-idle backend?
 *
 * @return Non-zero when tiku_clock_tickless_begin() can stretch (weak
 *         default 0)
 */
int tiku_clock_tickless_available(void);

#endif /* TIKU_CLOCK_H_ */
