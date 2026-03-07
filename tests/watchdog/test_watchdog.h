/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_watchdog.h - Watchdog timer test interface
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

#ifndef TEST_WATCHDOG_H
#define TEST_WATCHDOG_H

#include "tiku.h"
#include "kernel/cpu/tiku_common.h"

/*---------------------------------------------------------------------------*/
/* SHARED TEST CONSTANTS                                                     */
/*---------------------------------------------------------------------------*/

#define TEST_WATCHDOG_MAX_KICKS     30
#define TEST_WATCHDOG_KICK_INTERVAL 500  /* milliseconds */
#define TEST_WATCHDOG_DELAY_NORMAL  2500  /* milliseconds */

/*---------------------------------------------------------------------------*/
/* TEST FUNCTION DECLARATIONS                                                */
/*---------------------------------------------------------------------------*/

void test_watchdog_basic(void);
void test_watchdog_pause_resume(void);
void test_watchdog_interval_timer(void);
void test_watchdog_timeout(void);

#endif /* TEST_WATCHDOG_H */
