/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - SPI master driver for MSP430 eUSCI modules
 *
 * Implements blocking SPI master transactions in 3-pin mode (CS is
 * managed by the application via GPIO). The eUSCI module used is
 * selected by the board header via TIKU_BOARD_SPI_MODULE:
 *
 *   0 = eUSCI_A1 (default — used by FR5969 LaunchPad)
 *   1 = eUSCI_B1 (used by FR5994 LaunchPad's BoosterPack header)
 *
 * The pin routing for the chosen module comes from the board
 * header's TIKU_BOARD_SPI_PINS_INIT() macro. Both UCA and UCB
 * modules expose the same SPI-relevant register set (CTLW0 / BRW /
 * IFG / IE / RXBUF / TXBUF / STATW), so a single driver
 * implementation covers both via preprocessor register-name
 * substitution — same pattern tiku_uart_arch.c uses for UCA0/UCA1.
 *
 * SPI mode mapping (MSP430 UCCKPH is inverted vs standard CPHA):
 *   Mode 0 (CPOL=0 CPHA=0) → UCCKPL=0 UCCKPH=1
 *   Mode 1 (CPOL=0 CPHA=1) → UCCKPL=0 UCCKPH=0
 *   Mode 2 (CPOL=1 CPHA=0) → UCCKPL=1 UCCKPH=1
 *   Mode 3 (CPOL=1 CPHA=1) → UCCKPL=1 UCCKPH=0
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

#include "tiku_spi_arch.h"
#include "tiku.h"

/*---------------------------------------------------------------------------*/
/* MODULE SELECTION                                                          */
/*---------------------------------------------------------------------------*/

/*
 * Board header sets TIKU_BOARD_SPI_MODULE to choose which eUSCI
 * module backs the SPI bus. Default is 0 (eUSCI_A1) for backward
 * compatibility with the FR5969-era code that hardcoded UCA1.
 */
#ifndef TIKU_BOARD_SPI_MODULE
#define TIKU_BOARD_SPI_MODULE 0
#endif

#if TIKU_BOARD_SPI_MODULE == 0
/* eUSCI_A1 (FR5969 LaunchPad uses this on P2.5/P2.6/P2.7). */
#define TIKU_SPI_CTLW0   UCA1CTLW0
#define TIKU_SPI_BRW     UCA1BRW
#define TIKU_SPI_IFG     UCA1IFG
#define TIKU_SPI_RXBUF   UCA1RXBUF
#define TIKU_SPI_TXBUF   UCA1TXBUF
#elif TIKU_BOARD_SPI_MODULE == 1
/* eUSCI_B1 (FR5994 LaunchPad exposes this on P5.0/P5.1/P5.2 via
 * the BoosterPack-standard SPI header). */
#define TIKU_SPI_CTLW0   UCB1CTLW0
#define TIKU_SPI_BRW     UCB1BRW
#define TIKU_SPI_IFG     UCB1IFG
#define TIKU_SPI_RXBUF   UCB1RXBUF
#define TIKU_SPI_TXBUF   UCB1TXBUF
#else
#error "TIKU_BOARD_SPI_MODULE must be 0 (eUSCI_A1) or 1 (eUSCI_B1)"
#endif

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/** Busy-wait loop iteration limit to prevent infinite hangs. */
#define SPI_TIMEOUT     10000U

/** Dummy byte sent during read operations. */
#define SPI_DUMMY_BYTE  0xFF

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Map standard SPI mode number to MSP430 UCCKPH | UCCKPL bits.
 *
 * MSP430's UCCKPH is inverted relative to the standard CPHA definition:
 *   UCCKPH=1 → sample on first edge (standard CPHA=0)
 *   UCCKPH=0 → sample on second edge (standard CPHA=1)
 *
 * @param mode  TIKU_SPI_MODE_0 .. TIKU_SPI_MODE_3
 * @return Bitmask for UCAxCTLW0
 */
static uint16_t
spi_mode_to_bits(uint8_t mode)
{
    switch (mode) {
    case TIKU_SPI_MODE_0: return UCCKPH;             /* CPOL=0 CPHA=0 */
    case TIKU_SPI_MODE_1: return 0;                  /* CPOL=0 CPHA=1 */
    case TIKU_SPI_MODE_2: return UCCKPH | UCCKPL;    /* CPOL=1 CPHA=0 */
    case TIKU_SPI_MODE_3: return UCCKPL;             /* CPOL=1 CPHA=1 */
    default:              return UCCKPH;             /* fallback: mode 0 */
    }
}

/**
 * @brief Perform one full-duplex byte exchange on eUSCI_A1.
 *
 * @param tx  Byte to transmit
 * @return Received byte, or 0 on timeout
 */
static uint8_t
spi_xfer_byte(uint8_t tx)
{
    uint16_t timeout;

    timeout = SPI_TIMEOUT;
    while (!(TIKU_SPI_IFG & UCTXIFG)) {
        if (--timeout == 0) {
            return 0;
        }
    }
    TIKU_SPI_TXBUF = tx;

    timeout = SPI_TIMEOUT;
    while (!(TIKU_SPI_IFG & UCRXIFG)) {
        if (--timeout == 0) {
            return 0;
        }
    }
    return (uint8_t)TIKU_SPI_RXBUF;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize eUSCI_A1 for 3-pin SPI master operation.
 *
 * Clock source is SMCLK. The prescaler divides SMCLK to produce
 * the SPI clock (e.g. 8 MHz / 8 = 1 MHz).
 */
int
tiku_spi_arch_init(const tiku_spi_config_t *config)
{
    /* Select SPI function on board-specific SCLK/SIMO/SOMI pins */
    TIKU_BOARD_SPI_PINS_INIT();

    /* Put eUSCI_A1 in reset before configuration */
    TIKU_SPI_CTLW0 = UCSWRST;

    /* SPI master, synchronous, 3-pin mode, SMCLK source */
    TIKU_SPI_CTLW0 |= UCMST | UCSYNC | UCMODE_0 | UCSSEL__SMCLK;

    /* Set clock polarity and phase for requested SPI mode */
    TIKU_SPI_CTLW0 |= spi_mode_to_bits(config->mode);

    /* Set bit order */
    if (config->bit_order == TIKU_SPI_MSB_FIRST) {
        TIKU_SPI_CTLW0 |= UCMSB;
    }

    /* Set clock prescaler */
    TIKU_SPI_BRW = config->prescaler;

    /* Release from reset — SPI is now active */
    TIKU_SPI_CTLW0 &= ~UCSWRST;

    SPI_PRINTF("init: mode=%u order=%s prescaler=%u\n",
               config->mode,
               config->bit_order == TIKU_SPI_MSB_FIRST ? "MSB" : "LSB",
               config->prescaler);

    return TIKU_SPI_OK;
}

/**
 * @brief Place eUSCI_A1 in software reset (SPI disabled).
 */
void
tiku_spi_arch_close(void)
{
    TIKU_SPI_CTLW0 |= UCSWRST;
    SPI_PRINTF("close\n");
}

/**
 * @brief Single-byte full-duplex transfer.
 */
uint8_t
tiku_spi_arch_transfer(uint8_t tx_byte)
{
    return spi_xfer_byte(tx_byte);
}

/**
 * @brief Transmit bytes, discarding received data.
 */
int
tiku_spi_arch_write(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint16_t timeout;

    for (i = 0; i < len; i++) {
        timeout = SPI_TIMEOUT;
        while (!(TIKU_SPI_IFG & UCTXIFG)) {
            if (--timeout == 0) {
                return TIKU_SPI_ERR_TIMEOUT;
            }
        }
        TIKU_SPI_TXBUF = buf[i];

        timeout = SPI_TIMEOUT;
        while (!(TIKU_SPI_IFG & UCRXIFG)) {
            if (--timeout == 0) {
                return TIKU_SPI_ERR_TIMEOUT;
            }
        }
        (void)TIKU_SPI_RXBUF;   /* read to clear UCRXIFG */
    }

    return TIKU_SPI_OK;
}

/**
 * @brief Receive bytes, sending 0xFF as dummy data.
 */
int
tiku_spi_arch_read(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint16_t timeout;

    for (i = 0; i < len; i++) {
        timeout = SPI_TIMEOUT;
        while (!(TIKU_SPI_IFG & UCTXIFG)) {
            if (--timeout == 0) {
                return TIKU_SPI_ERR_TIMEOUT;
            }
        }
        TIKU_SPI_TXBUF = SPI_DUMMY_BYTE;

        timeout = SPI_TIMEOUT;
        while (!(TIKU_SPI_IFG & UCRXIFG)) {
            if (--timeout == 0) {
                return TIKU_SPI_ERR_TIMEOUT;
            }
        }
        buf[i] = (uint8_t)TIKU_SPI_RXBUF;
    }

    return TIKU_SPI_OK;
}

/**
 * @brief Full-duplex bulk transfer.
 */
int
tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                          uint16_t len)
{
    uint16_t i;
    uint16_t timeout;

    for (i = 0; i < len; i++) {
        timeout = SPI_TIMEOUT;
        while (!(TIKU_SPI_IFG & UCTXIFG)) {
            if (--timeout == 0) {
                return TIKU_SPI_ERR_TIMEOUT;
            }
        }
        TIKU_SPI_TXBUF = tx_buf[i];

        timeout = SPI_TIMEOUT;
        while (!(TIKU_SPI_IFG & UCRXIFG)) {
            if (--timeout == 0) {
                return TIKU_SPI_ERR_TIMEOUT;
            }
        }
        rx_buf[i] = (uint8_t)TIKU_SPI_RXBUF;
    }

    return TIKU_SPI_OK;
}
