/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - MSP430FR5969 CPU common functions
 *
 * This file provides MSP430FR5969-specific hardware definitions and
 * function prototypes for common CPU operations.
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

#ifndef TIKU_CPU_COMMON_H_
#define TIKU_CPU_COMMON_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"

#ifdef PLATFORM_MSP430

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup TIKU_BOARD_GPIO Board GPIO Definitions
 * @brief Generic GPIO macros that delegate to the active board header
 *
 * The actual pin assignments are defined in the board header selected
 * via tiku_device_select.h (e.g. tiku_board_fr5969_launchpad.h).
 * Old MSP430_FR5969_* names are kept as backward-compatible aliases.
 * @{
 */

/* Backward-compatible aliases -- map old names to new board macros */
#define MSP430_FR5969_LED1_INIT()       TIKU_BOARD_LED1_INIT()
#define MSP430_FR5969_LED1_ON()         TIKU_BOARD_LED1_ON()
#define MSP430_FR5969_LED1_OFF()        TIKU_BOARD_LED1_OFF()
#define MSP430_FR5969_LED1_TOGGLE()     TIKU_BOARD_LED1_TOGGLE()

#define MSP430_FR5969_LED2_INIT()       TIKU_BOARD_LED2_INIT()
#define MSP430_FR5969_LED2_ON()         TIKU_BOARD_LED2_ON()
#define MSP430_FR5969_LED2_OFF()        TIKU_BOARD_LED2_OFF()
#define MSP430_FR5969_LED2_TOGGLE()     TIKU_BOARD_LED2_TOGGLE()

#define MSP430_FR5969_BTN1_INIT()       TIKU_BOARD_BTN1_INIT()
#define MSP430_FR5969_BTN1_PRESSED()    TIKU_BOARD_BTN1_PRESSED()

#define MSP430_FR5969_BTN2_INIT()       TIKU_BOARD_BTN2_INIT()
#define MSP430_FR5969_BTN2_PRESSED()    TIKU_BOARD_BTN2_PRESSED()

/** @} */ /* End of TIKU_BOARD_GPIO group */

#endif /* PLATFORM_MSP430 */

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                         */
/*---------------------------------------------------------------------------*/

/* None */

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief MSP430-specific delay function
 * @param ms Number of milliseconds to delay
 */
void tiku_cpu_msp430_delay_ms(unsigned int ms);

#endif /* TIKU_CPU_COMMON_H_ */
