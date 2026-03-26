/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_common.c - Common utility functions
 *
 * Platform-independent utility functions including LED control and
 * delay functions. All hardware access is delegated to the HAL.
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

#include "tiku_common.h"

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Delay for specified number of milliseconds
 *
 * Delegates to the HAL for platform-specific busy-wait.
 *
 * @param ms Number of milliseconds to delay
 */
void tiku_common_delay_ms(unsigned int ms)
{
    tiku_common_arch_delay_ms(ms);
}

/**
 * @brief Initialize LED1 hardware
 *
 * Configures LED1 GPIO pin as output via the HAL.
 */
void tiku_common_led1_init(void)
{
    tiku_common_arch_led1_init();
}

/**
 * @brief Initialize LED2 hardware
 *
 * Configures LED2 GPIO pin as output via the HAL.
 */
void tiku_common_led2_init(void)
{
    tiku_common_arch_led2_init();
}

/**
 * @brief Turn on LED1
 */
void tiku_common_led1_on(void)
{
    tiku_common_arch_led1_on();
}

/**
 * @brief Turn on LED2
 */
void tiku_common_led2_on(void)
{
    tiku_common_arch_led2_on();
}

/**
 * @brief Turn off LED1
 */
void tiku_common_led1_off(void)
{
    tiku_common_arch_led1_off();
}

/**
 * @brief Turn off LED2
 */
void tiku_common_led2_off(void)
{
    tiku_common_arch_led2_off();
}

/**
 * @brief Toggle LED1 state
 */
void tiku_common_led1_toggle(void)
{
    tiku_common_arch_led1_toggle();
}

/**
 * @brief Toggle LED2 state
 */
void tiku_common_led2_toggle(void)
{
    tiku_common_arch_led2_toggle();
}
