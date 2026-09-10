/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - MSP430 CPU common functions
 *
 * This file provides MSP430-specific implementations of common
 * CPU functions including delay routines and hardware abstractions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_cpu_common.h"
#include <string.h>
#include <hal/tiku_cpu.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

/* Busy-delay loop counts scale from the original 8 MHz calibration. These
 * remain approximate: loop overhead limits accuracy at the lowest rates. */

/** @brief The rate to scale a busy delay by, 8 MHz before clock setup. */
static unsigned long delay_hz(void)
{
    unsigned long hz = tiku_cpu_mclk_hz();

    /* Called before cpu_freq_msp430_init() the live rate reads zero, and
     * a delay scaled from that is a thousand times too short. */
    return hz ? hz : 8000000UL;
}

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
    unsigned int loops = (unsigned int)(delay_hz() / 8000UL);
    if (!loops) loops = 1;

    for (i = 0; i < ms; i++) {

        for (j = 0; j < loops; j++) {

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
    unsigned int loops = (unsigned int)(delay_hz() / 2000000UL);
    if (!loops) loops = 1;

    while (us--) {
        for (i = 0; i < loops; i++) {
            __no_operation();
        }
    }
}

/*---------------------------------------------------------------------------*/

/** Boot-time reset cause (captured once before SYSRSTIV auto-clears) */
static uint16_t boot_rstiv;

/** Flag so SYSRSTIV is captured only on the first call */
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

/*
 * MSP430 TLV die record: lot/wafer id at 0x01A0A (4 bytes), die X at 0x01A0E
 * and die Y at 0x01A10 (2 each).  Concatenated, they give an 8-byte unique id.
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
