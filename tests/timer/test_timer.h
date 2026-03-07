/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_timer.h - Timer subsystem test interface
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

#ifndef TEST_TIMER_H_
#define TEST_TIMER_H_

#include "tiku.h"
#include "kernel/cpu/tiku_common.h"

/*---------------------------------------------------------------------------*/
/* SHARED TEST CONSTANTS                                                     */
/*---------------------------------------------------------------------------*/

#define TEST_TIMER_INTERVAL     (TIKU_CLOCK_SECOND)      /* 1 second */
#define TEST_TIMER_SHORT        (TIKU_CLOCK_SECOND / 4)  /* 250 ms */
#define TEST_TIMER_PERIODIC_CNT 3
#define TEST_TIMER_DRAIN_MAX    500  /* max scheduler loops */

#define TEST_HTIMER_PERIOD      (TIKU_HTIMER_SECOND / 10)  /* 100 ms */
#define TEST_HTIMER_REPEAT_CNT  5

/*---------------------------------------------------------------------------*/
/* TEST FUNCTION DECLARATIONS                                                */
/*---------------------------------------------------------------------------*/

/* Software timer tests */
void test_timer_event(void);
void test_timer_callback(void);
void test_timer_periodic(void);
void test_timer_stop(void);

/* Hardware timer tests */
void test_htimer_basic(void);
void test_htimer_periodic(void);

#endif /* TEST_TIMER_H_ */
