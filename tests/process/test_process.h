/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_process.h - Process subsystem test interface
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

#ifndef TEST_PROCESS_H_
#define TEST_PROCESS_H_

#include "tiku.h"
#include "kernel/cpu/tiku_common.h"

/*---------------------------------------------------------------------------*/
/* SHARED TEST CONSTANTS                                                     */
/*---------------------------------------------------------------------------*/

#define TEST_EVENT_CUSTOM   (TIKU_EVENT_USER + 1)
#define TEST_NUM_EVENTS     5
#define TEST_NUM_POLLS      3

/*---------------------------------------------------------------------------*/
/* TEST FUNCTION DECLARATIONS                                                */
/*---------------------------------------------------------------------------*/

void test_process_lifecycle(void);
void test_process_events(void);
void test_process_yield(void);
void test_process_broadcast(void);
void test_process_poll(void);
void test_process_queue_query(void);
void test_process_local_storage(void);
void test_process_broadcast_exit(void);
void test_process_graceful_exit(void);
void test_process_current_cleared(void);

#endif /* TEST_PROCESS_H_ */
