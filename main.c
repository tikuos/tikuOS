/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * main.c - Main application entry point
 *
 * System initialization and main event loop for the Tiku Operating System.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"
#include "boot/tiku_boot.h"
#include "kernel/scheduler/tiku_sched.h"

#if TEST_ENABLE
#include "tests/test_runner.h"
#endif

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Main application entry point
 * @return Should never return (infinite loop)
 *
 * Initializes the system hardware, optionally runs the test suite
 * when TEST_ENABLE is set, then enters the main application loop.
 */
int main(void) {
  int ret;

  /* Step 1: Disable watchdog immediately (before any other init) */
  tiku_watchdog_off();

  /* Step 2: Full system boot sequence
   *   - CPU frequency configuration
   *   - UART init (enables printf under GCC; no-op under CCS)
   *   - Clock initialization
   *   - Process subsystem, hardware timer, software timers (via scheduler)
   */
  ret = tiku_cpu_full_init(MAIN_CPU_FREQ);
  if (ret != TIKU_BOOT_SUCCESS) {
    MAIN_PRINTF("ERROR: Boot failed at stage %d\n", ret);
    while (1) { /* halt */ }
  }

  MAIN_PRINTF("TikuOS starting up...\n");

  MAIN_PRINTF("Boot complete\n");

#if TEST_ENABLE
  test_run_all();
#endif

  /* Step 3: Enter the scheduler loop (dispatches events, runs protothreads) */
  MAIN_PRINTF("Entering scheduler\n");
  tiku_sched_loop();

  /* Should never reach here */
  return 0;
}
