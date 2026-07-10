/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - nRF54L system-tick (TIMER10 compare)
 *
 * The system clock runs at TIKU_CLOCK_ARCH_SECOND ticks per second, driven by
 * a 32-bit TIMER peripheral (TIMER10) at 16 MHz with a COMPARE0->CLEAR short:
 * CC0 = 16 MHz / 128 = 125000 gives an EXACT 128 Hz tick (~7.8 ms), unlike a
 * SysTick tick (SysTick is reserved here for busy-delays, since the DWT cycle
 * counter freezes without a debugger).  A low-power GRTC-based tick is a
 * planned follow-up; TIMER10 stops in deep sleep, so it is a bring-up choice,
 * not the final low-power tick.
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

/**
 * @brief TIMER10 source clock in Hz.
 *
 * PRESCALER = 0 selects the 16 MHz peripheral clock.  16 MHz / 128 = 125000
 * fits the 32-bit TIMER comfortably and divides evenly, so the tick is exact.
 */
#define TIKU_NORDIC_TIMER_HZ    16000000UL

/** @brief TIMER CC0 value for one tick period (exact at 16 MHz / 128 Hz). */
#define TIKU_CLOCK_ARCH_INTERVAL  (TIKU_NORDIC_TIMER_HZ / TIKU_CLOCK_ARCH_SECOND)

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
