/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock.c - System clock implementation
 *
 * Thin wrappers around the architecture-specific clock functions.
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

#include "tiku_clock.h"
#include <tiku.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

void tiku_clock_init(void)
{
    CLOCK_ARCH_PRINTF("Init\n");
    tiku_clock_arch_init();
    CLOCK_ARCH_PRINTF("Init complete\n");
}

/*---------------------------------------------------------------------------*/

tiku_clock_time_t tiku_clock_time(void)
{
    return (tiku_clock_time_t)tiku_clock_arch_time();
}

/*---------------------------------------------------------------------------*/

unsigned long tiku_clock_seconds(void)
{
    return tiku_clock_arch_seconds();
}

/*---------------------------------------------------------------------------*/

void tiku_clock_wait(tiku_clock_time_t t)
{
    tiku_clock_arch_wait((tiku_clock_arch_time_t)t);
}

/*---------------------------------------------------------------------------*/

void tiku_clock_delay_usec(unsigned int dt)
{
    tiku_clock_arch_delay(dt);
}

/*---------------------------------------------------------------------------*/
