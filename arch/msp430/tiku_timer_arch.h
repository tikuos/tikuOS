/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - MSP430 timer architecture interface
 *
 * The system clock on Timer A0 from ACLK: tick and second counts, sub-tick
 * reads, busy-wait delays and tick conversions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_TIMER_ARCH_H_
#define TIKU_TIMER_ARCH_H_

#include <msp430.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
/** Tick count: ticks since tiku_clock_arch_init(). */
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/** Raw Timer A0 count (TA0R). */
typedef unsigned int  tiku_clock_arch_counter_t;

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128  /**< Ticks per second; power of 2 */
#endif

/** Clock tick frequency, in ticks per second */
#define TIKU_CLOCK_ARCH_SECOND TIKU_CLOCK_ARCH_CONF_SECOND

/** ACLK in Hz: the LFXT crystal, or REFO on the FR2433. */
#define TIKU_CLOCK_ARCH_ACLK_FREQ 32768

/** ACLK cycles per tick: the Timer A0 count between interrupts */
#define TIKU_CLOCK_ARCH_INTERVAL \
    (TIKU_CLOCK_ARCH_ACLK_FREQ / TIKU_CLOCK_ARCH_SECOND)

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the architecture-specific system clock
 *
 * Starts ACLK (LFXT, or REFO on the FR2433) and Timer A0 at
 * TIKU_CLOCK_ARCH_SECOND ticks per second, from a count of 0.
 *
 * @note Call once during system initialization.
 */
void tiku_clock_arch_init(void);

/**
 * @brief Get current clock time in ticks
 * @return Current tick count
 */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/**
 * @brief Get current time in seconds
 * @return Seconds since system start
 */
unsigned long tiku_clock_arch_seconds(void);

/**
 * @brief Set the system time
 * @param clock Clock ticks to set
 * @param fclock Timer A0 count (TA0R) to load
 */
void tiku_clock_arch_set(tiku_clock_arch_time_t clock,
                         tiku_clock_arch_time_t fclock);

/**
 * @brief Set the seconds counter
 * @param sec Seconds value to set
 */
void tiku_clock_arch_set_seconds(unsigned long sec);

/**
 * @brief Busy-wait for @p t clock ticks
 * @param t Number of ticks to wait
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/**
 * @brief CPU delay loop
 * @param i Delay units, each four NOPs plus the loop overhead
 */
void tiku_clock_arch_delay(unsigned int i);

/**
 * @brief Get fine-grained clock value
 * @return Timer A0 count since the last tick
 */
unsigned short tiku_clock_arch_fine(void);

/**
 * @brief Get maximum fine clock value
 * @return ACLK cycles per tick (TIKU_CLOCK_ARCH_INTERVAL)
 */
int tiku_clock_arch_fine_max(void);

/**
 * @brief Get raw timer counter value
 * @return Current timer counter
 */
tiku_clock_arch_counter_t tiku_clock_arch_counter(void);

/**
 * @brief Convert milliseconds to clock ticks
 * @param ms Milliseconds
 * @return Number of clock ticks
 * @note The multiplication uses an unsigned long intermediate.
 */
#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    ((tiku_clock_arch_time_t)(((unsigned long)(ms) * TIKU_CLOCK_ARCH_SECOND) / 1000UL))

/**
 * @brief Convert clock ticks to milliseconds
 * @param ticks Clock ticks
 * @return Milliseconds
 * @note The product with 1000 has the type of @p ticks.
 */
#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    ((unsigned long)(((ticks) * 1000) / TIKU_CLOCK_ARCH_SECOND))

#endif /* TIKU_TIMER_ARCH_H_ */
