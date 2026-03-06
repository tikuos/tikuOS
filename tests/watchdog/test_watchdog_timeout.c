/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_watchdog_timeout.c - Watchdog timeout (system reset) test
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

#if TEST_WDT_TIMEOUT

/*
 * Test 4: Watchdog Timeout (System Reset)
 * WARNING: This will reset your device!
 */
void test_watchdog_timeout(void)
{
    TEST_PRINTF("Starting timeout test - SYSTEM WILL RESET!\n");

    TEST_PRINTF("\n=== Test 4: Watchdog Timeout Demo ===\n");
    TEST_PRINTF("WARNING: System will reset in 3 seconds!\n");
    TEST_PRINTF("Disconnect debugger if needed.\n\n");

    /* Give time to read the message */
    delay_ms(2000);

    tiku_watchdog_config(
        TIKU_WDT_MODE_WATCHDOG,
        TIKU_WDT_SRC_ACLK,
        WDTIS__512,               /* ~15ms timeout */
        0,
        1
    );

    TEST_PRINTF("Watchdog configured for immediate timeout\n");
    TEST_PRINTF("Watchdog started with ~15ms timeout\n");
    TEST_PRINTF("NOT kicking watchdog - reset imminent!\n");
    TEST_PRINTF("Waiting for system reset...\n");

    /* Busy wait without kicking - will cause reset */
    while(1) {
        tiku_common_led1_toggle();
        tiku_common_delay_ms(10);
        /* Deliberately NOT calling tiku_watchdog_kick() */
    }
}

#endif /* TEST_WDT_TIMEOUT */

#endif /* PLATFORM_MSP430 */
