/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - MSP430 timer architecture interface
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file tiku_timer_arch.h
 * @brief MSP430FR5969 architecture-specific clock implementation
 *
 * System clock functionality using Timer A0 on MSP430FR5969.
 * Provides tick counting, delays, and time measurement.
 */

#ifndef TIKU_TIMER_ARCH_H_
#define TIKU_TIMER_ARCH_H_

#include <msp430.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

typedef unsigned int  tiku_clock_arch_counter_t;

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128  /* Must be power of 2 */
#endif

/** Clock tick frequency */
#define TIKU_CLOCK_ARCH_SECOND TIKU_CLOCK_ARCH_CONF_SECOND

#define TIKU_CLOCK_ARCH_ACLK_FREQ 32768 /* 32.768 kHz XT1 crystal */

/** Timer interval (ticks between interrupts) */
#define TIKU_CLOCK_ARCH_INTERVAL \
    (TIKU_CLOCK_ARCH_ACLK_FREQ / TIKU_CLOCK_ARCH_SECOND)

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the architecture-specific system clock
 *
 * Sets up Timer A0 for system tick generation on MSP430FR5969.
 * Must be called during system initialization.
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
 * @param fclock Fine clock value
 */
void tiku_clock_arch_set(tiku_clock_arch_time_t clock,
                         tiku_clock_arch_time_t fclock);

/**
 * @brief Set the seconds counter
 * @param sec Seconds value to set
 */
void tiku_clock_arch_set_seconds(unsigned long sec);

/**
 * @brief Delay for specified clock ticks
 * @param t Number of ticks to wait
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/**
 * @brief CPU delay loop
 * @param i Delay units (approximately 2.83us each at 8MHz)
 */
void tiku_clock_arch_delay(unsigned int i);

/**
 * @brief Get fine-grained clock value
 * @return Timer counter value within current tick
 */
unsigned short tiku_clock_arch_fine(void);

/**
 * @brief Get maximum fine clock value
 * @return Maximum fine clock count (interval size)
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
 */
#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    ((tiku_clock_arch_time_t)(((ms) * TIKU_CLOCK_ARCH_SECOND) / 1000))

/**
 * @brief Convert clock ticks to milliseconds
 * @param ticks Clock ticks
 * @return Milliseconds
 */
#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    ((unsigned long)(((ticks) * 1000) / TIKU_CLOCK_ARCH_SECOND))

#endif /* TIKU_TIMER_ARCH_H_ */
