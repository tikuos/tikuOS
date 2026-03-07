/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock.h - System clock interface
 *
 * Provides tick counting, time queries, and delay functions.
 * Delegates to architecture-specific implementations via the HAL.
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

/**
 * @def TIKU_CLOCK_LT(a, b)
 * @brief Wraparound-safe less-than comparison
 */
#define TIKU_CLOCK_LT(a, b) ((signed short)((a) - (b)) < 0)

/**
 * @def TIKU_CLOCK_DIFF(a, b)
 * @brief Wraparound-safe difference (a - b)
 */
#define TIKU_CLOCK_DIFF(a, b) ((signed short)((a) - (b)))

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

#endif /* TIKU_CLOCK_H_ */
