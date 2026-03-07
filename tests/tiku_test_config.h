/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_test_config.h - Test framework configuration
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
 * @file tiku_test_config.h
 * @brief Test configuration flags for enabling/disabling test modules
 *
 * Set TEST_ENABLE to 1 and enable individual test flags to run
 * specific tests from the test suite.
 */

#ifndef TIKU_TEST_CONFIG_H_
#define TIKU_TEST_CONFIG_H_

/**
 * @defgroup TIKU_TEST_CONFIG Test Configuration Flags
 * @brief Configuration flags for enabling/disabling test modules
 * @{
 */

/** Master test enable - set to 1 to run test suite from main */
#define TEST_ENABLE 1

/*---------------------------------------------------------------------------*/
/* WATCHDOG TESTS                                                            */
/*---------------------------------------------------------------------------*/

/** Enable watchdog timer tests */
#define TEST_WATCHDOG 0

/** Enable basic watchdog operation test */
#define TEST_WDT_BASIC 0

/** Enable watchdog pause/resume test */
#define TEST_WDT_PAUSE_RESUME 0

/** Enable interval timer mode test */
#define TEST_WDT_INTERVAL 0

/** Enable watchdog timeout test (WARNING: This will reset the device!) */
#define TEST_WDT_TIMEOUT 0

/*---------------------------------------------------------------------------*/
/* CPU CLOCK TESTS                                                           */
/*---------------------------------------------------------------------------*/

#define TEST_CPUCLOCK 0

/*---------------------------------------------------------------------------*/
/* PROCESS TESTS                                                             */
/*---------------------------------------------------------------------------*/

/** Enable basic process lifecycle test */
#define TEST_PROCESS_LIFECYCLE 0

/** Enable event posting test */
#define TEST_PROCESS_EVENTS 0

/** Enable cooperative yield test */
#define TEST_PROCESS_YIELD 0

/** Enable broadcast event test */
#define TEST_PROCESS_BROADCAST 0

/** Enable process poll test */
#define TEST_PROCESS_POLL 0

/** Enable queue query function test */
#define TEST_PROCESS_QUEUE 0

/** Enable process local storage test */
#define TEST_PROCESS_LOCAL 0

/** Enable broadcast exit safety test (list corruption fix) */
#define TEST_PROCESS_BROADCAST_EXIT 0

/** Enable graceful exit vs force exit test */
#define TEST_PROCESS_GRACEFUL_EXIT 0

/** Enable current process cleared after dispatch test */
#define TEST_PROCESS_CURRENT_CLEARED 0

/** Auto-derived: true if any process test is enabled */
#define TEST_PROCESS (TEST_PROCESS_LIFECYCLE || TEST_PROCESS_EVENTS ||    \
                      TEST_PROCESS_YIELD || TEST_PROCESS_BROADCAST ||     \
                      TEST_PROCESS_POLL || TEST_PROCESS_QUEUE ||          \
                      TEST_PROCESS_LOCAL || TEST_PROCESS_BROADCAST_EXIT ||\
                      TEST_PROCESS_GRACEFUL_EXIT ||                       \
                      TEST_PROCESS_CURRENT_CLEARED)

/*---------------------------------------------------------------------------*/
/* TIMER TESTS                                                               */
/*---------------------------------------------------------------------------*/

/** Enable timer subsystem tests */
#define TEST_TIMER 0

/** Enable event timer test */
#define TEST_TIMER_EVENT 0

/** Enable callback timer test */
#define TEST_TIMER_CALLBACK 0

/** Enable periodic timer test */
#define TEST_TIMER_PERIODIC 0

/** Enable timer stop test */
#define TEST_TIMER_STOP 0

/** Enable hardware timer basic test */
#define TEST_HTIMER_BASIC 0

/** Enable hardware timer periodic test */
#define TEST_HTIMER_PERIODIC 0

/*---------------------------------------------------------------------------*/
/* MEMORY TESTS                                                              */
/*---------------------------------------------------------------------------*/

/** Enable arena creation and initial stats test */
#define TEST_MEM_CREATE 0

/** Enable basic allocation and pointer correctness test */
#define TEST_MEM_ALLOC 0

/** Enable alignment of odd-sized requests test */
#define TEST_MEM_ALIGNMENT 0

/** Enable arena full returns NULL test */
#define TEST_MEM_FULL 0

/** Enable reset restores offset but preserves peak test */
#define TEST_MEM_RESET 0

/** Enable peak tracking across resets test */
#define TEST_MEM_PEAK 0

/** Enable null and zero-size inputs rejected test */
#define TEST_MEM_INVALID 0

/** Enable secure reset zeros memory test */
#define TEST_MEM_SECURE_RESET 0

/** Enable two independent arenas test */
#define TEST_MEM_TWO_ARENAS 0

/** Auto-derived: true if any arena memory test is enabled */
#define TEST_MEM (TEST_MEM_CREATE || TEST_MEM_ALLOC ||                     \
                  TEST_MEM_ALIGNMENT || TEST_MEM_FULL ||                   \
                  TEST_MEM_RESET || TEST_MEM_PEAK ||                       \
                  TEST_MEM_INVALID || TEST_MEM_SECURE_RESET ||             \
                  TEST_MEM_TWO_ARENAS)

/*---------------------------------------------------------------------------*/
/* PERSISTENT STORE TESTS                                                    */
/*---------------------------------------------------------------------------*/

/** Enable persist init on zeroed store test */
#define TEST_PERSIST_INIT        0

/** Enable persist register and count test */
#define TEST_PERSIST_REGISTER    1

/** Enable persist write then read back test */
#define TEST_PERSIST_WRITE_READ  0

/** Enable persist read with too-small buffer test */
#define TEST_PERSIST_SMALL_BUF   0

/** Enable persist write exceeding capacity test */
#define TEST_PERSIST_OVERFLOW    0

/** Enable persist read non-existent key test */
#define TEST_PERSIST_NOT_FOUND   0

/** Enable persist delete entry test */
#define TEST_PERSIST_DELETE      0

/** Enable persist store full test */
#define TEST_PERSIST_FULL        0

/** Enable persist reboot survival test */
#define TEST_PERSIST_REBOOT      0

/** Enable persist wear check test */
#define TEST_PERSIST_WEAR        0

/** Enable persist register same key twice test */
#define TEST_PERSIST_DUP_KEY     0

/** Auto-derived: true if any persistent store test is enabled */
#define TEST_PERSIST (TEST_PERSIST_INIT || TEST_PERSIST_REGISTER ||        \
                      TEST_PERSIST_WRITE_READ || TEST_PERSIST_SMALL_BUF || \
                      TEST_PERSIST_OVERFLOW || TEST_PERSIST_NOT_FOUND ||   \
                      TEST_PERSIST_DELETE || TEST_PERSIST_FULL ||          \
                      TEST_PERSIST_REBOOT || TEST_PERSIST_WEAR ||         \
                      TEST_PERSIST_DUP_KEY)

/** @} */ /* End of TIKU_TEST_CONFIG group */

#endif /* TIKU_TEST_CONFIG_H_ */
