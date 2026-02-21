/*
 * Tiku Operating System
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

#endif /* TIKU_WATCHDOG_HAL_H_ */
