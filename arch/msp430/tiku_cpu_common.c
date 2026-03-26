/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - MSP430FR5969 CPU common functions
 *
 * This file provides MSP430FR5969-specific implementations of common
 * CPU functions including delay routines and hardware abstractions.
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

#include "tiku_cpu_common.h"

/*---------------------------------------------------------------------------*/
/* PRIVATE CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

/** Approximate loop iterations per millisecond at 8MHz */
#define TIKU_DELAY_LOOPS_PER_MS    1000

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
 * @brief Delay for a specified number of milliseconds
 * @param ms Number of milliseconds to delay
 *
 * This function provides a software-based delay using nested loops.
 * The delay is approximate and depends on CPU frequency and compiler
 * optimization settings.
 *
 * @note This is a blocking delay function
 * @warning Not suitable for precise timing requirements
 * @warning Accuracy varies with CPU frequency and optimization level
 */

void tiku_cpu_msp430_delay_ms(unsigned int ms)
{
    unsigned int i, j;

    for (i = 0; i < ms; i++) {

        for (j = 0; j < TIKU_DELAY_LOOPS_PER_MS; j++) {

            __no_operation();

        }

    }
}

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTIONS                                                        */
/*---------------------------------------------------------------------------*/

/* None */
