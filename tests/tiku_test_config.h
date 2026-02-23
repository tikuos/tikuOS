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
#define TEST_PROCESS_QUEUE 1

/** Auto-derived: true if any process test is enabled */
#define TEST_PROCESS (TEST_PROCESS_LIFECYCLE || TEST_PROCESS_EVENTS || \
                      TEST_PROCESS_YIELD || TEST_PROCESS_BROADCAST || \
                      TEST_PROCESS_POLL || TEST_PROCESS_QUEUE)

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

/** @} */ /* End of TIKU_TEST_CONFIG group */

#endif /* TIKU_TEST_CONFIG_H_ */
