/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog.h - Watchdog timer interface
 *
 * Platform-independent watchdog timer API. All hardware access is
 * delegated to the HAL (tiku_watchdog_hal.h).
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

#ifndef TIKU_WATCHDOG_H_
#define TIKU_WATCHDOG_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <hal/tiku_watchdog_hal.h>

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure the watchdog timer with custom parameters
 *
 * Sets mode, clock source, interval, and startup behaviour. Only
 * available on platforms whose HAL exposes these parameters.
 *
 * @param mode          Watchdog or interval timer mode
 * @param clk           Clock source selection
 * @param interval      Timeout interval
 * @param start_held    If non-zero, start in held (paused) state
 * @param kick_on_start If non-zero, kick the timer on start
 */
void tiku_watchdog_config(tiku_wdt_mode_t mode, tiku_wdt_clk_t clk,
                         tiku_wdt_interval_t interval, int start_held,
                         int kick_on_start);

/** @brief Initialize the watchdog timer with default settings */
void tiku_watchdog_init(void);

/** @brief Kick (reset) the watchdog timer to prevent timeout */
void tiku_watchdog_kick(void);

/** @brief Pause the watchdog timer */
void tiku_watchdog_pause(void);

/** @brief Resume the watchdog timer */
void tiku_watchdog_resume(void);

/** @brief Resume the watchdog timer with an immediate kick */
void tiku_watchdog_resume_with_kick(void);

/** @brief Return "watchdog" or "interval" for the current WDT mode */
const char *tiku_watchdog_mode_str(void);

/** @brief Return the current watchdog mode */
tiku_wdt_mode_t tiku_watchdog_get_mode(void);

/** @brief Return the current clock source */
tiku_wdt_clk_t tiku_watchdog_get_clk(void);

/** @brief Return the current interval divider */
tiku_wdt_interval_t tiku_watchdog_get_interval(void);

/** @brief Disable the watchdog timer entirely */
void tiku_watchdog_off(void);

#endif /* TIKU_WATCHDOG_H_ */
