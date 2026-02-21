/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog.h - Watchdog timer interface
 *
 * Platform-independent watchdog timer API. Delegates to
 * architecture-specific implementations.
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

#include <hal/tiku_watchdog_hal.h>

#ifdef PLATFORM_MSP430
// Function prototype for MSP430-specific configuration
void tiku_watchdog_config(tiku_wdt_mode_t mode, tiku_wdt_clk_t clk,
                         tiku_wdt_interval_t interval, int start_held,
                         int kick_on_start);
#endif

// Platform-independent function prototypes
void tiku_watchdog_init(void);
void tiku_watchdog_kick(void);
void tiku_watchdog_pause(void);
void tiku_watchdog_resume(void);
void tiku_watchdog_resume_with_kick(void);
void tiku_watchdog_off(void);

#endif /* TIKU_WATCHDOG_H_ */
