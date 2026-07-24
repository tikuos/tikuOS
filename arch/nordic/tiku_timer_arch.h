/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - nRF54L system-tick (GRTC by default; TIMER10 fallback)
 *
 * The system clock runs at TIKU_CLOCK_ARCH_SECOND ticks per second.  Default
 * source is the GRTC (Global RTC): its 1 MHz SYSCOUNTER is already running
 * (started by the boot ROM) and lives in the always-on low-frequency domain,
 * so the tick keeps firing through deep sleep -- the low-power foundation.  A
 * compare channel (CC0) is armed each tick by an alternating 7812/7813-count
 * step, averaging exactly 7812.5 = 128 Hz with no drift, and re-armed relative
 * to the previous compare so ISR latency never accumulates.  SysTick stays
 * free for busy-delays (the DWT cycle counter freezes without a debugger).
 *
 * Build with -DTIKU_NORDIC_TICK_TIMER10 to fall back to the bring-up tick: a
 * 32-bit TIMER10 at 16 MHz, CC0=125000, COMPARE0->CLEAR short (exact 128 Hz
 * but stops when HFCLK gates in deep sleep).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_TIMER_ARCH_H_
#define TIKU_NORDIC_TIMER_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Clock tick counter type (ticks since boot; wraps ~387 days). */
#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/** @brief Fine-resolution sub-tick counter type (captured TIMER residue). */
typedef unsigned int tiku_clock_arch_counter_t;

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief System tick frequency in Hz (must be a power of 2).
 *
 * Default 128 Hz matches the MSP430 / rp2350 / ambiq ports (~7.8 ms/tick).
 * Override via -DTIKU_CLOCK_ARCH_CONF_SECOND=<n>.
 */
#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128   /* must be a power of 2 */
#endif

/** @brief Resolved tick frequency — use this in code. */
#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND

/*
 * Tick source: GRTC (default) or TIMER10 (fallback, -DTIKU_NORDIC_TICK_TIMER10).
 * The source-specific clock rate + per-tick interval live in tiku_timer_arch.c
 * (arch-internal; the kernel only speaks TIKU_CLOCK_ARCH_SECOND).  GRTC counts
 * at 1 MHz (7812.5 counts/tick -> exact 128 Hz via an alternating interval);
 * TIMER10 at 16 MHz (125000 counts/tick, exact).
 */

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Configure TIMER10 for TIKU_CLOCK_ARCH_SECOND Hz and enable it. */
void                   tiku_clock_arch_init(void);

/** @brief Current tick counter (incremented by the TIMER10 compare ISR). */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/** @brief Elapsed whole seconds since boot (plus any set_seconds base). */
unsigned long          tiku_clock_arch_seconds(void);

/** @brief Overwrite the seconds counter (RTC sync). */
void                   tiku_clock_arch_set_seconds(unsigned long sec);

/** @brief Busy-wait until the tick counter reaches absolute value @p t. */
void                   tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/** @brief Busy-wait for at least @p us microseconds (SysTick busy-delay). */
void                   tiku_clock_arch_delay(unsigned int us);

/** @brief Sub-tick TIMER residue for fine-resolution timing (0..fine_max). */
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
