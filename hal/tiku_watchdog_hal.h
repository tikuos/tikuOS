/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog_hal.h - Platform-routing header for watchdog timer
 *
 * Routes to the correct architecture-specific watchdog header based
 * on the selected platform. This is the single point where the arch
 * watchdog header enters the include chain.
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

#ifndef TIKU_WATCHDOG_HAL_H_
#define TIKU_WATCHDOG_HAL_H_

#ifdef PLATFORM_MSP430
#include "arch/msp430/tiku_cpu_watchdog_arch.h"
#endif

/*---------------------------------------------------------------------------*/
/* HAL-NAMED INTERVAL CONSTANTS                                              */
/*---------------------------------------------------------------------------*/

/*
 * Platform-neutral aliases for the watchdog interval divider. Each
 * resolves to the underlying device register value via the arch
 * header included above. Kernel code should use these names instead
 * of WDTIS__* (or any other platform-specific symbol) so that the
 * tree compiles unchanged when ported.
 *
 * Interval semantics: divider applied to the WDT clock source,
 * giving the time-to-timeout in clock ticks. Divider 32768 with a
 * 32 768 Hz ACLK source = ~1 s timeout.
 */
#define TIKU_WDT_INTERVAL_64        WDTIS__64
#define TIKU_WDT_INTERVAL_512       WDTIS__512
#define TIKU_WDT_INTERVAL_8192      WDTIS__8192
#define TIKU_WDT_INTERVAL_32768     WDTIS__32768

#define TIKU_WDT_INTERVAL_DEFAULT   TIKU_WDT_INTERVAL_32768

/*---------------------------------------------------------------------------*/
/* HAL-to-arch mapping macros                                                */
/*---------------------------------------------------------------------------*/

#define tiku_watchdog_arch_on(src, isel) \
    tiku_cpu_msp430_watchdog_on_arch((src), (isel))
#define tiku_watchdog_arch_off() \
    tiku_cpu_msp430_watchdog_off_arch()
#define tiku_watchdog_arch_kick() \
    tiku_cpu_msp430_watchdog_kick_arch()
#define tiku_watchdog_arch_pause() \
    tiku_cpu_msp430_watchdog_pause_arch()
#define tiku_watchdog_arch_resume(kick) \
    tiku_cpu_msp430_watchdog_resume_arch(kick)

#endif /* TIKU_WATCHDOG_HAL_H_ */
