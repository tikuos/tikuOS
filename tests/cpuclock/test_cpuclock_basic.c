/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_cpuclock_basic.c - CPU clock basic test
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

#include "test_cpuclock.h"


void test_cpuclock_basic(void)
{
    TEST_PRINTF("Starting CPU clock test\n");


#ifdef PLATFORM_MSP430

    TEST_PRINTF("Configuring P3.4 for clock output\n");

    P3DIR |= BIT4;    // Make P3.4 an output
    P3SEL1 |= BIT4;   // Select special function (clock output)
    P3SEL0 |= BIT4;

    TEST_PRINTF("P3.4 configured for clock output\n");

#endif

    TEST_PRINTF("CPU clock test completed\n");
}
