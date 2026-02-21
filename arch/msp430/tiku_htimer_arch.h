/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.h - MSP430 hardware timer architecture interface
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
 * @file tiku_htimer_arch.h
 * @brief MSP430FR5969 hardware timer architecture header
 *
 * MSP430-specific htimer header. Includes the platform configuration
 * (for TIKU_HTIMER_ARCH_SECOND) and declares MSP430-only functions.
 */

#ifndef TIKU_HTIMER_ARCH_H_
#define TIKU_HTIMER_ARCH_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_htimer_config.h"
#include <kernel/timers/tiku_htimer.h>

/* Verify that configuration defined the required macro */
#ifndef TIKU_HTIMER_ARCH_SECOND
#error "TIKU_HTIMER_ARCH_SECOND not defined by tiku_htimer_config.h"
#endif

/*---------------------------------------------------------------------------*/
/* MSP430-SPECIFIC FUNCTIONS                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure ACLK source if using ACLK for timer
 *
 * Should be called before tiku_htimer_arch_init() if the timer
 * is configured to use ACLK.
 */
void tiku_htimer_arch_configure_aclk(void);

/**
 * @brief Check if timer interrupt is pending
 * @return Non-zero if interrupt flag is set
 */
int tiku_htimer_arch_interrupt_pending(void);

/**
 * @brief Get timer configuration register value
 * @return Timer control register (for debugging)
 */
unsigned int tiku_htimer_arch_get_timer_config(void);

/**
 * @brief Print current timer configuration
 *
 * Displays clock source, dividers, and calculated frequency
 * for debugging purposes.
 */
void tiku_htimer_arch_print_config(void);

/**
 * @brief Reset timer counter to zero
 * @warning Use with caution - will disrupt timing!
 */
void tiku_htimer_arch_reset_counter(void);

#endif /* TIKU_HTIMER_ARCH_H_ */
