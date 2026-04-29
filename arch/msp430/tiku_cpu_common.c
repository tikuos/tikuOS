/*
 * Tiku Operating System v0.03
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
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

/** Approximate loop iterations per millisecond at 8MHz */
#define TIKU_DELAY_LOOPS_PER_MS    1000

/** Approximate loop iterations per microsecond at 8MHz.
 *  At 8 MHz each NOP is ~125 ns; one loop iteration (NOP + branch)
 *  is ~2 cycles = 250 ns, so 4 iterations per microsecond. */
#define TIKU_DELAY_LOOPS_PER_US    4

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

/**
 * @brief Delay for a specified number of microseconds
 * @param us Number of microseconds to delay
 */
void tiku_cpu_msp430_delay_us(unsigned int us)
{
    unsigned int i;

    while (us--) {
        for (i = 0; i < TIKU_DELAY_LOOPS_PER_US; i++) {
            __no_operation();
        }
    }
}

/*---------------------------------------------------------------------------*/

/** Boot-time reset cause (captured once before SYSRSTIV auto-clears) */
static uint16_t boot_rstiv;

/** Flag so we capture SYSRSTIV only on the first call */
static uint8_t  rstiv_captured;

uint16_t
tiku_cpu_msp430_reset_reason(void)
{
    if (!rstiv_captured) {
        boot_rstiv = SYSRSTIV;
        rstiv_captured = 1;
    }
    return boot_rstiv;
}

/*---------------------------------------------------------------------------*/

/**
 * MSP430 TLV die-record layout (all devices):
 *   0x01A0A  lot/wafer ID (4 bytes)
 *   0x01A0E  die X pos   (2 bytes)
 *   0x01A10  die Y pos   (2 bytes)
 *
 * We concatenate lot + X + Y for an 8-byte unique ID.
 */
uint8_t
tiku_cpu_msp430_unique_id(uint8_t *buf, uint8_t len)
{
    /* TLV die-record base address (same on FR2433/FR5969/FR5994) */
    const uint8_t *die = (const uint8_t *)0x01A0AU;
    uint8_t n = (len < 8) ? len : 8;

    if (buf == (uint8_t *)0) {
        return 0;
    }
    memcpy(buf, die, n);
    return n;
}
