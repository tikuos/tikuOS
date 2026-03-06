/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_watchdog_pause_resume.c - Watchdog pause/resume test
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

#if TEST_WDT_PAUSE_RESUME

/*
 * Test 2: Watchdog Pause/Resume
 * - Start watchdog
 * - Pause for extended operation
 * - Resume watchdog
 */
void test_watchdog_pause_resume(void)
{
    TEST_PRINTF("Starting pause/resume test\n");

    TEST_PRINTF("\n=== Test 2: Watchdog Pause/Resume ===\n");

    /* Start watchdog with short timeout */
    #ifdef PLATFORM_MSP430
    tiku_watchdog_config(
        TIKU_WDT_MODE_WATCHDOG,
        TIKU_WDT_SRC_ACLK,
        WDTIS__8192,              /* ~250ms timeout */
        0,
        1
    );
    #else
    tiku_watchdog_init();
    #endif

    TEST_PRINTF("Watchdog started with ~250ms timeout\n");

    /* Run with kicks for a while */
    for (int i = 0; i < 5; i++) {
        tiku_watchdog_kick();
        tiku_common_led1_toggle();
        TEST_PRINTF("Kick %d\n", i + 1);
        tiku_common_delay_ms(100);
    }

    /* Pause watchdog for extended operation */
    TEST_PRINTF("Pausing watchdog for long operation\n");
    TEST_PRINTF("\nPausing watchdog for extended operation...\n");
    tiku_watchdog_pause();

    /* Simulate long operation (would normally cause reset) */
    for (int i = 0; i < 10; i++) {
        tiku_common_led2_toggle();
        TEST_PRINTF("Long operation %d/10 (WDT paused)\n", i + 1);
        tiku_common_delay_ms(300);  /* Longer than WDT timeout */
    }

    /* Resume watchdog with kick */
    TEST_PRINTF("Resuming watchdog operation\n");
    TEST_PRINTF("\nResuming watchdog with kick...\n");
    tiku_watchdog_resume_with_kick();

    /* Continue normal operation */
    for (int i = 0; i < 5; i++) {
        tiku_watchdog_kick();
        tiku_common_led1_toggle();
        TEST_PRINTF("Post-resume kick %d\n", i + 1);
        tiku_common_delay_ms(100);
    }

    TEST_PRINTF("Pause/resume test completed successfully\n");
    TEST_PRINTF("\nPause/Resume test completed successfully!\n");
    tiku_watchdog_off();
}

#endif /* TEST_WDT_PAUSE_RESUME */

#endif /* PLATFORM_MSP430 */
