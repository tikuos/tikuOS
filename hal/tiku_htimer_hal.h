/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_hal.h - Hardware abstraction layer interface for hardware timers
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
 * @file tiku_htimer_hal.h
 * @brief Platform-agnostic hardware timer interface
 *
 * Declares the functions and constants that each platform must
 * provide to support the htimer (hardware timer) subsystem.
 * No platform-specific headers are included.
 *
 * The platform implementation (e.g. arch/msp430/tiku_htimer_arch.c)
 * provides the actual timer hardware control.
 */

#ifndef TIKU_HTIMER_HAL_H_
#define TIKU_HTIMER_HAL_H_

#ifdef PLATFORM_MSP430
#include "arch/msp430/tiku_htimer_config.h"
#endif

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * The htimer kernel module requires three arch functions:
 *   - tiku_htimer_arch_init()
 *   - tiku_htimer_arch_schedule(tiku_htimer_clock_t t)
 *   - tiku_htimer_arch_now()
 *
 * These are declared in tiku_htimer.h and must be provided by
 * the platform.
 *
 * The platform must also define TIKU_HTIMER_ARCH_SECOND to
 * indicate the hardware timer tick frequency.
 */

/*---------------------------------------------------------------------------*/
/* PLATFORM ISR CONTRACT                                                     */
/*---------------------------------------------------------------------------*/

/**
 * The platform timer ISR must call tiku_htimer_run_next() when
 * the compare-match interrupt fires. This dispatches the pending
 * htimer callback and handles rescheduling.
 *
 * tiku_htimer_run_next() is declared in tiku_htimer.h.
 */

#endif /* TIKU_HTIMER_HAL_H_ */
