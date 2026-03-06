/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_watchdog_basic.c - Basic watchdog operation test
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

#include "test_watchdog.h"

#ifdef PLATFORM_MSP430

#if TEST_WDT_BASIC

static volatile unsigned int wdt_kick_count = 0;

/*
 * Test 1: Basic Watchdog Operation
 * - Configure watchdog with ~1 second timeout
 * - Kick watchdog periodically to prevent reset
 * - LED blinks to show system is running
 */
void test_watchdog_basic(void)
{
    TEST_PRINTF("Starting basic watchdog test\n");

    TEST_PRINTF("\n=== Test 1: Basic Watchdog Operation ===\n");
    TEST_PRINTF("Watchdog will be kicked every 500ms\n");
    TEST_PRINTF("LED1 will blink to show system is alive\n\n");

    /* Configure watchdog:
     * - Watchdog mode (not interval)
     * - ACLK source (32768 Hz)
     * - ~1 second timeout (32768 cycles)
     */
    tiku_watchdog_config(
        TIKU_WDT_MODE_WATCHDOG,  /* Watchdog mode */
        TIKU_WDT_SRC_ACLK,        /* Use ACLK (32.768 kHz) */
        WDTIS__32768,             /* ~1 second timeout */
        0,                        /* Start immediately */
        1                         /* Kick on start */
    );

    TEST_PRINTF("Watchdog configured and started\n");
    TEST_PRINTF("Watchdog started with ~1 second timeout\n");

    /* Main loop - kick watchdog periodically */
    while(1) {
        /* Kick the watchdog */
        tiku_watchdog_kick();
        wdt_kick_count++;

        /* Toggle LED and print status every 10 kicks */
        if (wdt_kick_count % 10 == 0) {
           tiku_common_led1_toggle();
           TEST_PRINTF("WDT kicked %d times, system alive\n", wdt_kick_count);
        }

        /* Delay 500ms (half the watchdog timeout) */
       tiku_common_delay_ms(TEST_WATCHDOG_DELAY_NORMAL);

        /* Stop after 30 kicks for demo */
        if (wdt_kick_count >= 30) {
            TEST_PRINTF("Basic watchdog test completed after %d kicks\n", wdt_kick_count);
            TEST_PRINTF("\nBasic watchdog test completed successfully!\n");
            tiku_watchdog_off();
            break;
        }
    }
}

#endif /* TEST_WDT_BASIC */

#endif /* PLATFORM_MSP430 */
