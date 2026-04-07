/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock_hal.h - Hardware abstraction layer interface for system clock
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
 * @file tiku_clock_hal.h
 * @brief Platform-agnostic clock architecture interface
 *
 * Declares the functions that each platform must implement to provide
 * system clock functionality. No platform-specific headers are included.
 */

#ifndef TIKU_CLOCK_HAL_H_
#define TIKU_CLOCK_HAL_H_

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @typedef tiku_clock_arch_time_t
 * @brief Architecture-specific clock time type
 *
 * Platforms provide this via their arch header (e.g. tiku_timer_arch.h).
 * This fallback applies only if no arch header has defined it.
 */
#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize platform-specific clock hardware
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
 * @brief Set the seconds counter
 * @param sec Seconds value to set
 */
void tiku_clock_arch_set_seconds(unsigned long sec);

/**
 * @brief Busy-wait for specified clock ticks
 * @param t Number of ticks to wait
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/**
 * @brief CPU delay loop
 * @param i Delay units (platform-specific calibration)
 */
void tiku_clock_arch_delay(unsigned int i);

/**
 * @brief Get fine-grained clock value
 * @return Timer counter value within current tick
 */
unsigned short tiku_clock_arch_fine(void);

/**
 * @brief Get maximum fine clock value
 * @return Maximum fine clock count
 */
int tiku_clock_arch_fine_max(void);

#endif /* TIKU_CLOCK_HAL_H_ */
