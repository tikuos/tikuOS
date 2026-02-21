/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_common.c - Common utility functions
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_common.h"

#ifdef PLATFORM_MSP430
#include "arch/msp430/tiku_cpu_common.h"
#endif

/*---------------------------------------------------------------------------*/
/* PRIVATE CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

/* None */

/*---------------------------------------------------------------------------*/
/* PRIVATE TYPES                                                            */
/*---------------------------------------------------------------------------*/

/* None */

/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                        */
/*---------------------------------------------------------------------------*/

/* None */

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTION PROTOTYPES                                              */
/*---------------------------------------------------------------------------*/

/* None */

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Delay for specified number of milliseconds
 * @param ms Number of milliseconds to delay
 * 
 * Platform-independent delay function that delegates to
 * the appropriate platform-specific implementation.
 * 
 * @note Accuracy depends on platform implementation
 * @warning Not suitable for precise timing requirements
 */
void 
tiku_common_delay_ms(unsigned int ms)
{
    #ifdef PLATFORM_MSP430
        tiku_cpu_msp430_delay_ms(ms);
    #endif
}

/**
 * @brief Initialize LED1 hardware
 * 
 * Configures LED1 GPIO pin as output and sets initial state to off.
 * Platform-specific implementation handles actual pin configuration.
 */
void 
tiku_common_led1_init(void)
{
    #ifdef PLATFORM_MSP430
        TIKU_BOARD_LED1_INIT();
    #endif
}

/**
 * @brief Initialize LED2 hardware
 * 
 * Configures LED2 GPIO pin as output and sets initial state to off.
 * Platform-specific implementation handles actual pin configuration.
 */
void 
tiku_common_led2_init(void)
{
    #ifdef PLATFORM_MSP430
        TIKU_BOARD_LED2_INIT();
    #endif
}

/**
 * @brief Turn on LED1
 * 
 * Sets LED1 to the illuminated state by setting the
 * appropriate GPIO pin high.
 */
void 
tiku_common_led1_on(void)
{
    #ifdef PLATFORM_MSP430
        TIKU_BOARD_LED1_ON();
    #endif
}

/**
 * @brief Turn on LED2
 * 
 * Sets LED2 to the illuminated state by setting the
 * appropriate GPIO pin high.
 */
void 
tiku_common_led2_on(void)
{
    #ifdef PLATFORM_MSP430
        TIKU_BOARD_LED2_ON();
    #endif
}

/**
 * @brief Turn off LED2
 * 
 * Sets LED2 to the off state by clearing the
 * appropriate GPIO pin.
 */
void 
tiku_common_led2_off(void)
{
    #ifdef PLATFORM_MSP430
        TIKU_BOARD_LED2_OFF();
    #endif
}

/**
 * @brief Turn off LED1
 * 
 * Sets LED1 to the off state by clearing the
 * appropriate GPIO pin.
 */
void 
tiku_common_led1_off(void)
{
    #ifdef PLATFORM_MSP430
        TIKU_BOARD_LED1_OFF();
    #endif
}

/**
 * @brief Toggle LED1 state
 * 
 * Inverts the current state of LED1. If LED1 is on,
 * it will be turned off, and vice versa.
 */
void 
tiku_common_led1_toggle(void)
{
    #ifdef PLATFORM_MSP430
        TIKU_BOARD_LED1_TOGGLE();
    #endif
}

/**
 * @brief Toggle LED2 state
 * 
 * Inverts the current state of LED2. If LED2 is on,
 * it will be turned off, and vice versa.
 */
void 
tiku_common_led2_toggle(void)
{
    #ifdef PLATFORM_MSP430
        TIKU_BOARD_LED2_TOGGLE();
    #endif
}

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTIONS                                                        */
/*---------------------------------------------------------------------------*/

/* None */
