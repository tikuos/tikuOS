/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - 1-Wire bus driver for MSP430 (GPIO bit-bang)
 *
 * Implements the Dallas/Maxim 1-Wire protocol using GPIO bit-banging
 * on the pin specified by TIKU_BOARD_OW_* macros in the board header.
 * Timing is derived from an 8 MHz MCLK using __delay_cycles().
 *
 * An external 4.7 kohm pull-up resistor is required on the data line.
 * Interrupts are disabled during timing-critical operations to ensure
 * correct bit timing.
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

#include "tiku_onewire_arch.h"
#include "tiku.h"

#ifdef TIKU_BOARD_OW_AVAILABLE  /* Board supports 1-Wire */

/*---------------------------------------------------------------------------*/
/* TIMING MACROS                                                             */
/*---------------------------------------------------------------------------*/

/**
 * Microsecond delay assuming 8 MHz MCLK.
 * __delay_cycles() is available in both TI and GCC MSP430 toolchains.
 */
#define OW_DELAY_US(us)  __delay_cycles((unsigned long)(us) * 8UL)

/*---------------------------------------------------------------------------*/
/* GPIO HELPERS                                                              */
/*---------------------------------------------------------------------------*/

/** Drive the 1-Wire line low (output, low). */
static inline void
ow_drive_low(void)
{
    TIKU_BOARD_OW_OUT &= ~TIKU_BOARD_OW_BIT;
    TIKU_BOARD_OW_DIR |= TIKU_BOARD_OW_BIT;
}

/** Release the 1-Wire line (input, external pull-up drives high). */
static inline void
ow_release(void)
{
    TIKU_BOARD_OW_DIR &= ~TIKU_BOARD_OW_BIT;
}

/** Read the current state of the 1-Wire line. */
static inline uint8_t
ow_read(void)
{
    return (TIKU_BOARD_OW_IN & TIKU_BOARD_OW_BIT) ? 1 : 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the 1-Wire GPIO pin.
 *
 * Configures the pin as input (pull-up drives the line high),
 * clears the output latch, and ensures GPIO mode (SEL0=SEL1=0).
 */
int
tiku_onewire_arch_init(void)
{
    /* Start with line released (input, pull-up drives high) */
    TIKU_BOARD_OW_DIR &= ~TIKU_BOARD_OW_BIT;
    TIKU_BOARD_OW_OUT &= ~TIKU_BOARD_OW_BIT;

    /* Ensure pin is in GPIO mode (SEL0=SEL1=0) */
    TIKU_BOARD_OW_SEL0 &= ~TIKU_BOARD_OW_BIT;
    TIKU_BOARD_OW_SEL1 &= ~TIKU_BOARD_OW_BIT;

    return TIKU_OW_OK;
}

/** @brief Release the 1-Wire pin (set to input, external pull-up holds high). */
void
tiku_onewire_arch_close(void)
{
    /* Release the line */
    TIKU_BOARD_OW_DIR &= ~TIKU_BOARD_OW_BIT;
}

/**
 * @brief 1-Wire reset: 480 us low pulse, then listen for presence.
 *
 * Timing:
 *   Master: pull low 480 us, release
 *   Wait 15-60 us for line to go high (pull-up)
 *   Wait until ~70 us mark, then sample (device pulls low if present)
 *   Wait remaining 410 us for reset window to complete
 *
 * We first verify the line goes HIGH after release (pull-up working),
 * then check for the device pulling it LOW (presence pulse).
 */
int
tiku_onewire_arch_reset(void)
{
    uint8_t line_high;
    uint8_t presence;
    uint16_t sr;

    /* Save and disable interrupts for timing-critical section */
    sr = __get_SR_register() & GIE;
    __bic_SR_register(GIE);

    ow_drive_low();
    OW_DELAY_US(480);

    ow_release();
    OW_DELAY_US(15);

    /* Verify line went HIGH after release (pull-up is working) */
    line_high = ow_read();

    OW_DELAY_US(55);   /* Wait until ~70 us from release */

    /* Sample: low = device present */
    presence = ow_read();

    OW_DELAY_US(410);

    /* Restore interrupts */
    __bis_SR_register(sr);

    /* If line never went high, pull-up is missing or bus is stuck */
    if (!line_high) {
        return TIKU_OW_ERR_NO_DEVICE;
    }

    return presence ? TIKU_OW_ERR_NO_DEVICE : TIKU_OW_OK;
}

/**
 * @brief Write a single bit.
 *
 * Write-1 slot: pull low 2 us, release, wait 62 us
 * Write-0 slot: pull low 60 us, release, wait 4 us
 *
 * The DS18B20 samples at ~30 us from the falling edge.
 * A shorter write-1 pulse ensures the line is high at sample time.
 */
void
tiku_onewire_arch_write_bit(uint8_t bit)
{
    uint16_t sr;

    sr = __get_SR_register() & GIE;
    __bic_SR_register(GIE);

    if (bit & 1) {
        ow_drive_low();
        OW_DELAY_US(2);
        ow_release();
        OW_DELAY_US(62);
    } else {
        ow_drive_low();
        OW_DELAY_US(60);
        ow_release();
        OW_DELAY_US(4);
    }

    __bis_SR_register(sr);
}

/**
 * @brief Read a single bit.
 *
 * Read slot: pull low 2 us, release, wait 10 us, sample, wait 50 us
 * Total slot: ~62 us (>60 us minimum)
 *
 * The master must sample within 15 us of the falling edge.
 * A shorter initial pulse gives the pull-up more time to charge
 * the line before sampling.
 *
 * Note: On MSP430, the input register (PxIN) is synchronized through
 * a D-type flip-flop clocked by MCLK. After switching DIR from output
 * to input, the new line state may not appear in PxIN for up to
 * 2 MCLK cycles. The delay after release ensures correct sampling.
 */
uint8_t
tiku_onewire_arch_read_bit(void)
{
    uint8_t bit;
    uint16_t sr;

    sr = __get_SR_register() & GIE;
    __bic_SR_register(GIE);

    ow_drive_low();
    OW_DELAY_US(2);

    ow_release();
    OW_DELAY_US(10);

    /* Read PxIN directly as volatile to prevent any caching */
    bit = (*(volatile uint8_t *)&TIKU_BOARD_OW_IN & TIKU_BOARD_OW_BIT) ? 1 : 0;

    OW_DELAY_US(50);

    __bis_SR_register(sr);

    return bit;
}

/**
 * @brief Write a byte (LSB first).
 */
void
tiku_onewire_arch_write_byte(uint8_t byte)
{
    uint8_t i;

    for (i = 0; i < 8; i++) {
        tiku_onewire_arch_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

/**
 * @brief Read a byte (LSB first).
 */
uint8_t
tiku_onewire_arch_read_byte(void)
{
    uint8_t byte = 0;
    uint8_t i;

    for (i = 0; i < 8; i++) {
        byte |= (tiku_onewire_arch_read_bit() << i);
    }

    return byte;
}

#endif /* TIKU_BOARD_OW_AVAILABLE */
