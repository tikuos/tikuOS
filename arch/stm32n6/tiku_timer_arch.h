/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - STM32N6 kernel clock on LPTIM1.
 *
 * LPTIM1 runs from HSI by way of CLKP, so the tick rate does not change with
 * the CPU clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_TIMER_ARCH_H_
#define TIKU_STM32N6_TIMER_ARCH_H_

#include <stdint.h>

/**
 * @brief Monotonic tick count since boot.
 *
 * At 128 Hz a 32-bit counter wraps in about 388 days; compare ticks with the
 * wraparound-safe TIKU_CLOCK_LT and TIKU_CLOCK_DIFF (tiku_clock.h).
 */
#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/** @brief Sub-tick counter type, taken from the LPTIM1 count register. */
typedef unsigned int tiku_clock_arch_counter_t;

/**
 * @brief System tick frequency in Hz; must be a power of two.
 *
 * The default 128 Hz gives a 7.8 ms tick.
 */
#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128
#endif

/** @brief Tick frequency in Hz; code reads this, not the CONF_ form. */
#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND

/** @brief LPTIM1 prescaler as log2: 7 divides the 64 MHz HSI by 128. */
#define TIKU_STM32N6_LPTIM_PRESC_LOG2   7U      /* divide by 128 */

/** @brief LPTIM1 count rate, 500 kHz from HSI, independent of the CPU clock. */
#define TIKU_STM32N6_LPTIM_HZ           (64000000UL >> TIKU_STM32N6_LPTIM_PRESC_LOG2)

/** @brief LPTIM1 counts per tick: 500000 / 128 = 3906, a 128.008 Hz tick. */
#define TIKU_CLOCK_ARCH_INTERVAL \
    (TIKU_STM32N6_LPTIM_HZ / TIKU_CLOCK_ARCH_SECOND)

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Start LPTIM1 and zero the tick and seconds counters. */
void                   tiku_clock_arch_init(void);

/** @brief Ticks since boot. @return Monotonic tick count */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/** @brief Seconds since boot. @return Monotonic second count */
unsigned long          tiku_clock_arch_seconds(void);

/** @brief Set the seconds counter. @param sec  New value */
void                   tiku_clock_arch_set_seconds(unsigned long sec);

/** @brief Busy-wait until the tick counter reaches @p t. @param t  Deadline */
void                   tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/** @brief Busy-wait for microseconds off LPTIM1. @param us  Microseconds */
void                   tiku_clock_arch_delay(unsigned int us);

/** @brief Sub-tick position. @return LPTIM1 count within the current tick */
unsigned short         tiku_clock_arch_fine(void);

/** @brief Sub-tick range. @return One more than the maximum fine value */
int                    tiku_clock_arch_fine_max(void);

/** @brief Milliseconds to ticks, rounded up so a wait is never short. */
#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    (((unsigned long)(ms) * TIKU_CLOCK_ARCH_SECOND + 999UL) / 1000UL)

/** @brief Ticks to milliseconds. */
#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    (((unsigned long)(ticks) * 1000UL) / TIKU_CLOCK_ARCH_SECOND)

#endif /* TIKU_STM32N6_TIMER_ARCH_H_ */
