/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - nRF54L system tick (GRTC by default, TIMER10 fallback).
 *
 * The GRTC source keeps counting through deep sleep, so tickless idle can
 * stretch the tick; ticks are accounted against a half-count anchor, so the
 * 128 Hz rate is exact.  SysTick stays free for busy-delays.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_TIMER_ARCH_H_
#define TIKU_NORDIC_TIMER_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
/** @brief Clock tick counter type (ticks since boot; wraps after ~388 days). */
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/** @brief Fine-resolution sub-tick counter type. */
typedef unsigned int tiku_clock_arch_counter_t;

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief System tick frequency in Hz (must be a power of 2); default 128 Hz,
 *        ~7.8 ms per tick.
 *
 * Override via -DTIKU_CLOCK_ARCH_CONF_SECOND=<n>.  The GRTC tick is exact only
 * when 2 MHz / n is an integer, which holds up to 128 Hz.
 */
#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128   /* must be a power of 2 */
#endif

/** @brief Resolved tick frequency — use this in code. */
#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND

/*
 * Tick source: GRTC (default) or TIMER10 (-DTIKU_NORDIC_TICK_TIMER10).
 * The source-specific clock rate and per-tick interval are in
 * tiku_timer_arch.c; the kernel uses only TIKU_CLOCK_ARCH_SECOND.  GRTC counts
 * at 1 MHz (7812.5 counts/tick: half-count accounting alternates 7812 and
 * 7813); TIMER10 at 16 MHz (125000 counts/tick, exact).
 */

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Start the tick source at TIKU_CLOCK_ARCH_SECOND Hz and enable its
 *         interrupt. */
void                   tiku_clock_arch_init(void);

/** @brief Ticks since boot (advanced by the tick ISR and tickless code). */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/** @brief Elapsed whole seconds since boot (plus any set_seconds base). */
unsigned long          tiku_clock_arch_seconds(void);

/** @brief Overwrite the seconds counter (RTC sync). */
void                   tiku_clock_arch_set_seconds(unsigned long sec);

/** @brief Busy-wait until @p t more ticks have elapsed (a duration). */
void                   tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/** @brief Busy-wait for at least @p us microseconds (SysTick busy-delay). */
void                   tiku_clock_arch_delay(unsigned int us);

/** @brief Position within the current tick, scaled to 0..0xFFFF. */
unsigned short         tiku_clock_arch_fine(void);

/** @brief Maximum value of tiku_clock_arch_fine(). */
int                    tiku_clock_arch_fine_max(void);

/** @brief Convert milliseconds to tick counts (rounded down). */
#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    ((tiku_clock_arch_time_t)(((ms) * TIKU_CLOCK_ARCH_SECOND) / 1000))

/** @brief Convert tick counts to milliseconds (rounded down). */
#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    ((unsigned long)(((ticks) * 1000) / TIKU_CLOCK_ARCH_SECOND))

#endif /* TIKU_NORDIC_TIMER_ARCH_H_ */
