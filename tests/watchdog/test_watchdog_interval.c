/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_watchdog_interval.c - Interval timer mode test
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
#include <arch/msp430/tiku_compiler.h>

#ifdef PLATFORM_MSP430

#if TEST_WDT_INTERVAL

static volatile unsigned int interval_isr_count = 0;

/* Watchdog interval timer ISR */
TIKU_ISR(WDT_VECTOR, WDT_ISR)
{
    interval_isr_count++;
    tiku_common_led1_toggle();
}

/*
 * Test 3: Interval Timer Mode
 * - Configure watchdog as interval timer
 * - Generate periodic interrupts
 */
void test_watchdog_interval_timer(void)
{
    TEST_PRINTF("Starting interval timer test\n");

    TEST_PRINTF("\n=== Test 3: Interval Timer Mode ===\n");
    TEST_PRINTF("WDT will generate interrupts every ~250ms\n");
    TEST_PRINTF("LED2 will toggle on each interrupt\n\n");

    /* First, make sure WDT is off */
    tiku_watchdog_off();

    /* Configure for interval timer mode manually */
    WDTCTL = WDTPW | WDTHOLD;  /* Stop WDT */
    WDTCTL = WDTPW | WDTTMSEL | WDTCNTCL | WDTSSEL__ACLK | WDTIS__8192;

    /* Enable WDT interrupt */
    SFRIE1 |= WDTIE;

    TEST_PRINTF("Interval timer configured and started\n");
    TEST_PRINTF("Interval timer started\n");

    /* Main loop - just count and display */
    unsigned int last_isr_count = 0;
    for (int i = 0; i < 50; i++) {
        if (interval_isr_count != last_isr_count) {
            TEST_PRINTF("Interval ISR fired %u times\n", interval_isr_count);
            last_isr_count = interval_isr_count;
        }

        tiku_common_led1_toggle();
        tiku_common_delay_ms(100);
    }

    SFRIE1 &= ~WDTIE;  /* Disable interrupt */
    WDTCTL = WDTPW | WDTHOLD;  /* Stop WDT */

    TEST_PRINTF("Interval timer test completed with %u interrupts\n", interval_isr_count);
    TEST_PRINTF("\nInterval timer test completed!\n");
    TEST_PRINTF("Total interrupts: %u\n", interval_isr_count);
}

#endif /* TEST_WDT_INTERVAL */

#endif /* PLATFORM_MSP430 */
