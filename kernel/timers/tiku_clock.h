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
 * Override by defining TIKU_CLOCK_CONF_TIME_T before including this header.
 */
#ifdef TIKU_CLOCK_CONF_TIME_T
typedef TIKU_CLOCK_CONF_TIME_T tiku_clock_time_t;
#else
typedef unsigned short tiku_clock_time_t;
#endif

/*
 * HOW LONG AN INTERVAL THIS TYPE CAN MEASURE.
 *
 * The counter wraps, and the arithmetic below is wraparound-safe only for
 * intervals shorter than half its range: 256 s at 16 bits and 128 Hz, and a
 * plain difference is wrong past 512 s.  That is not hypothetical -- a 605 s
 * encode once reported 92 s, and before that a 25 s turn reported 3 489 178,
 * both from taking one difference across a wrap.
 *
 * MEASURE LONG THINGS AS A SUM OF SHORT DIFFERENCES, each taken in the
 * counter's own width, or use a cycle counter.  Widening this type is the
 * other option and costs a 16-bit MCU real work in every timer compare.
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
 * Both are expressed in the clock type's OWN width, with no fixed-width
 * cast anywhere.  A `signed short` cast here would be correct only while
 * the type is 16 bits, so overriding TIKU_CLOCK_CONF_TIME_T -- which the
 * typedef above openly invites -- would silently truncate every comparison
 * and every difference.
 */

/**
 * @def TIKU_CLOCK_LT(a, b)
 * @brief Wraparound-safe less-than comparison
 */
#define TIKU_CLOCK_LT(a, b) \
    ((tiku_clock_time_t)((a) - (b)) > TIKU_CLOCK_MAX_INTERVAL)

/**
 * @def TIKU_CLOCK_DIFF(a, b)
 * @brief Wraparound-safe difference (a - b)
 */
#define TIKU_CLOCK_DIFF(a, b)                                                 \
    (TIKU_CLOCK_LT((a), (b)) ? -(long)(tiku_clock_time_t)((b) - (a))          \
                             :  (long)(tiku_clock_time_t)((a) - (b)))

/**
 * @def TIKU_CLOCK_MS_TO_TICKS(ms)
 * @brief Convert milliseconds to clock ticks
 */
#define TIKU_CLOCK_MS_TO_TICKS(ms) \
    ((tiku_clock_time_t)(((ms) * TIKU_CLOCK_SECOND) / 1000))

/*---------------------------------------------------------------------------*/
/* CORE API                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the system clock
 *
 * Delegates to tiku_clock_arch_init(). Call once during system boot.
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
 * Called with interrupts masked before a tick-woken idle while timers are armed
 * but none due: the arch may program its compare straight to the next deadline
 * and accounts true elapsed ticks on wake, so the kernel clock stays exact.
 *
 * @param ticks_ahead Ticks until the next software-timer deadline (>1)
 * @return Non-zero if the stretch was armed, 0 if unsupported
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
 *         default 0).  Tests use this to pick between the per-tick
 *         and tickless wake-count expectations.
 */
int tiku_clock_tickless_available(void);

#endif /* TIKU_CLOCK_H_ */
