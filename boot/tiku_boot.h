/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot.h - System boot and initialization functions
 *
 * This file provides system boot sequence management and initialization
 * functions for the Tiku Operating System.
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

#ifndef TIKU_BOOT_H_
#define TIKU_BOOT_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_BOOT_SUCCESS
 * @brief Boot sequence completed successfully
 */
#define TIKU_BOOT_SUCCESS    0

/**
 * @def TIKU_BOOT_ERROR
 * @brief Boot sequence failed
 */
#define TIKU_BOOT_ERROR     -1

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @enum tiku_boot_stage_e
 * @brief Boot sequence stages
 */
typedef enum {
    TIKU_BOOT_STAGE_INIT = 0,      /**< Initial boot stage */
    TIKU_BOOT_STAGE_CPU,           /**< CPU initialization */
    TIKU_BOOT_STAGE_MEMORY,        /**< Memory initialization */
    TIKU_BOOT_STAGE_PERIPHERALS,   /**< Peripheral initialization */
    TIKU_BOOT_STAGE_SERVICES,      /**< System services */
    TIKU_BOOT_STAGE_COMPLETE       /**< Boot complete */
} tiku_boot_stage_e;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                      */
/*---------------------------------------------------------------------------*/


/**
 * @brief Perform complete system initialization
 * @param cpu_freq Target CPU frequency in MHz
 * @return TIKU_BOOT_SUCCESS on success, TIKU_BOOT_ERROR on failure
 * 
 * Performs full system initialization including CPU configuration,
 * memory setup, peripheral initialization, and system services.
 */
int tiku_cpu_full_init(unsigned int cpu_freq);

/**
 * @brief Get current boot stage
 * @return Current boot stage
 * 
 * Returns the current stage of the boot sequence for diagnostic purposes.
 */
tiku_boot_stage_e tiku_boot_get_stage(void);

/**
 * @brief Check if boot sequence is complete
 * @return Non-zero if boot is complete, zero otherwise
 * 
 * Returns whether the system has completed the boot sequence
 * and is ready for normal operation.
 */
int tiku_boot_is_complete(void);

#endif /* TIKU_BOOT_H_ */
