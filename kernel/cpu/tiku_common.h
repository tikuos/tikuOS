/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_common.h - Common utility functions
 *
 * This file provides common utility functions for the Tiku Operating System
 * including LED control, delay functions, and platform abstraction.
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

#ifndef TIKU_COMMON_H_
#define TIKU_COMMON_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"

#ifdef PLATFORM_MSP430
#include "arch/msp430/tiku_cpu_common.h"
#endif

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                     */
/*---------------------------------------------------------------------------*/

/* None */

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                         */
/*---------------------------------------------------------------------------*/

/* None */

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Delay for specified number of milliseconds
 * @param ms Number of milliseconds to delay
 */
void tiku_common_delay_ms(unsigned int ms);

/**
 * @brief Initialize LED1 hardware
 */
void tiku_common_led1_init(void);

/**
 * @brief Initialize LED2 hardware
 */
void tiku_common_led2_init(void);

/**
 * @brief Turn on LED1
 */
void tiku_common_led1_on(void);

/**
 * @brief Turn on LED2
 */
void tiku_common_led2_on(void);

/**
 * @brief Turn off LED1
 */
void tiku_common_led1_off(void);

/**
 * @brief Turn off LED2
 */
void tiku_common_led2_off(void);

/**
 * @brief Toggle LED1 state
 */
void tiku_common_led1_toggle(void);

/**
 * @brief Toggle LED2 state
 */
void tiku_common_led2_toggle(void);

#endif /* TIKU_COMMON_H_ */
