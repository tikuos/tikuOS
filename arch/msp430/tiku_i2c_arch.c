/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - I2C master driver for an MSP430 eUSCI_B module.
 *
 * Blocking master transactions, with the module, pin routing and clock
 * prescaler taken from the board header's TIKU_BOARD_I2C_* macros.  Every busy-wait gives up
 * after I2C_TIMEOUT polls, so a stuck bus cannot hang the caller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_i2c_arch.h"
#include "tiku.h"

#ifdef TIKU_BOARD_I2C_BRW_100K  /* Board supports I2C */

/*---------------------------------------------------------------------------*/
/* MODULE SELECTION                                                          */
/*---------------------------------------------------------------------------*/

/*
 * The board header's TIKU_BOARD_I2C_MODULE picks the eUSCI_B module behind
 * the bus: 0 (eUSCI_B0, the default) or 2 (eUSCI_B2).
 */
#ifndef TIKU_BOARD_I2C_MODULE
#define TIKU_BOARD_I2C_MODULE 0
#endif

#if TIKU_BOARD_I2C_MODULE == 0
#define TIKU_I2C_CTLW0   UCB0CTLW0
#define TIKU_I2C_CTLW1   UCB0CTLW1
#define TIKU_I2C_BRW     UCB0BRW
#define TIKU_I2C_I2CSA   UCB0I2CSA
#define TIKU_I2C_IFG     UCB0IFG
#define TIKU_I2C_RXBUF   UCB0RXBUF
#define TIKU_I2C_TXBUF   UCB0TXBUF
#elif TIKU_BOARD_I2C_MODULE == 2
#define TIKU_I2C_CTLW0   UCB2CTLW0
#define TIKU_I2C_CTLW1   UCB2CTLW1
#define TIKU_I2C_BRW     UCB2BRW
#define TIKU_I2C_I2CSA   UCB2I2CSA
#define TIKU_I2C_IFG     UCB2IFG
#define TIKU_I2C_RXBUF   UCB2RXBUF
#define TIKU_I2C_TXBUF   UCB2TXBUF
#else
#error "TIKU_BOARD_I2C_MODULE must be 0 (eUSCI_B0) or 2 (eUSCI_B2)"
#endif

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/** Busy-wait iteration limit. */
#define I2C_TIMEOUT     10000U

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Wait for a flag in TIKU_I2C_IFG with timeout and NACK detection.
 *
 * A NACK sends STOP, waits up to I2C_TIMEOUT polls for it to complete and
 * returns TIKU_I2C_ERR_NACK; a timeout requests STOP and returns
 * TIKU_I2C_ERR_TIMEOUT.
 *
 * @param flag  The interrupt flag bit to wait for (e.g. UCTXIFG0)
 * @return TIKU_I2C_OK if flag was set, negative error code otherwise
 */
static int
i2c_wait_flag(uint16_t flag)
{
    uint16_t timeout = I2C_TIMEOUT;

    while (!(TIKU_I2C_IFG & flag)) {
        if (TIKU_I2C_IFG & UCNACKIFG) {
            TIKU_I2C_IFG &= ~UCNACKIFG;
            TIKU_I2C_CTLW0 |= UCTXSTP;
            timeout = I2C_TIMEOUT;
            while ((TIKU_I2C_CTLW0 & UCTXSTP) && --timeout != 0) {
                /* wait for STOP to complete */
            }
            return TIKU_I2C_ERR_NACK;
        }
        if (--timeout == 0) {
            TIKU_I2C_CTLW0 |= UCTXSTP;
            return TIKU_I2C_ERR_TIMEOUT;
        }
    }

    return TIKU_I2C_OK;
}

/**
 * @brief Wait for STOP condition to complete with timeout.
 *
 * @return TIKU_I2C_OK if STOP completed, TIKU_I2C_ERR_TIMEOUT otherwise
 */
static int
i2c_wait_stop(void)
{
    uint16_t timeout = I2C_TIMEOUT;

    while (TIKU_I2C_CTLW0 & UCTXSTP) {
        if (--timeout == 0) {
            return TIKU_I2C_ERR_TIMEOUT;
        }
    }

    return TIKU_I2C_OK;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_i2c_arch_init(const tiku_i2c_config_t *config)
{
    /* Select I2C function on board-specific SDA/SCL pins */
    TIKU_BOARD_I2C_PINS_INIT();

    /* Put the module in reset before configuration */
    TIKU_I2C_CTLW0 = UCSWRST;

    /* I2C master mode, synchronous, SMCLK source */
    TIKU_I2C_CTLW0 |= UCMODE_3 | UCMST | UCSSEL__SMCLK | UCSYNC;

    /* Set clock prescaler for requested speed */
    if (config->speed == TIKU_I2C_SPEED_FAST) {
        TIKU_I2C_BRW = TIKU_BOARD_I2C_BRW_400K;
    } else {
        TIKU_I2C_BRW = TIKU_BOARD_I2C_BRW_100K;
    }

    /* UCASTP_0: no automatic STOP; the driver sends STOP itself */
    TIKU_I2C_CTLW1 = UCASTP_0;

    /* Release from reset — I2C is now active */
    TIKU_I2C_CTLW0 &= ~UCSWRST;

    I2C_PRINTF("init: speed=%s\n",
               config->speed == TIKU_I2C_SPEED_FAST ? "400k" : "100k");

    return TIKU_I2C_OK;
}

void
tiku_i2c_arch_close(void)
{
    TIKU_I2C_CTLW0 |= UCSWRST;
    I2C_PRINTF("close\n");
}

int
tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    int rc;

    /* Clear stale interrupt flags from any previous transaction */
    TIKU_I2C_IFG = 0;

    /* Set slave address */
    TIKU_I2C_I2CSA = addr;

    /* Transmitter mode, generate START + address */
    TIKU_I2C_CTLW0 |= UCTR | UCTXSTT;

    for (i = 0; i < len; i++) {
        /* Wait for TX buffer ready */
        rc = i2c_wait_flag(UCTXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }

        /* Load next byte */
        TIKU_I2C_TXBUF = buf[i];
    }

    /* Wait for final byte to shift out */
    rc = i2c_wait_flag(UCTXIFG0);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }

    /* Generate STOP */
    TIKU_I2C_CTLW0 |= UCTXSTP;

    return i2c_wait_stop();
}

int
tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint16_t timeout;
    int rc;

    /* Clear stale interrupt flags from any previous transaction */
    TIKU_I2C_IFG = 0;

    /* Set slave address */
    TIKU_I2C_I2CSA = addr;

    /* Receiver mode, generate START + address */
    TIKU_I2C_CTLW0 &= ~UCTR;
    TIKU_I2C_CTLW0 |= UCTXSTT;

    if (len == 1) {
        /*
         * Single-byte read: STOP goes out as soon as UCTXSTT clears,
         * while the byte is being received, so only one byte is clocked.
         */
        timeout = I2C_TIMEOUT;
        while ((TIKU_I2C_CTLW0 & UCTXSTT) && !(TIKU_I2C_IFG & UCNACKIFG)) {
            if (--timeout == 0) {
                TIKU_I2C_CTLW0 |= UCTXSTP;
                return TIKU_I2C_ERR_TIMEOUT;
            }
        }
        TIKU_I2C_CTLW0 |= UCTXSTP;

        rc = i2c_wait_flag(UCRXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        buf[0] = (uint8_t)TIKU_I2C_RXBUF;

        return i2c_wait_stop();
    }

    /* Multi-byte read */
    for (i = 0; i < len; i++) {
        /* Issue STOP before reading the last byte */
        if (i == len - 1) {
            TIKU_I2C_CTLW0 |= UCTXSTP;
        }

        rc = i2c_wait_flag(UCRXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        buf[i] = (uint8_t)TIKU_I2C_RXBUF;
    }

    return i2c_wait_stop();
}

int
tiku_i2c_arch_probe(uint8_t addr)
{
    int rc;

    /* Clear stale interrupt flags from any previous transaction */
    TIKU_I2C_IFG = 0;

    /* Set slave address */
    TIKU_I2C_I2CSA = addr;

    /* Transmitter mode, generate START + address (no data follows) */
    TIKU_I2C_CTLW0 |= UCTR | UCTXSTT;

    {
        uint16_t timeout = I2C_TIMEOUT;
        while ((TIKU_I2C_CTLW0 & UCTXSTT) && !(TIKU_I2C_IFG & UCNACKIFG)) {
            if (--timeout == 0u) {
                TIKU_I2C_CTLW0 |= UCTXSTP;
                return TIKU_I2C_ERR_TIMEOUT;
            }
        }
    }
    rc = (TIKU_I2C_IFG & UCNACKIFG) ? TIKU_I2C_ERR_NACK : TIKU_I2C_OK;
    TIKU_I2C_CTLW0 |= UCTXSTP;
    if (i2c_wait_stop() != TIKU_I2C_OK) {
        return rc == TIKU_I2C_ERR_NACK ? rc : TIKU_I2C_ERR_TIMEOUT;
    }
    if (TIKU_I2C_IFG & UCNACKIFG) {
        rc = TIKU_I2C_ERR_NACK;
    }
    return rc;
}

int
tiku_i2c_arch_write_read(uint8_t addr,
                          const uint8_t *tx_buf, uint16_t tx_len,
                          uint8_t *rx_buf, uint16_t rx_len)
{
    uint16_t i;
    uint16_t timeout;
    int rc;

    /* Clear stale interrupt flags from any previous transaction */
    TIKU_I2C_IFG = 0;

    /* --- Write phase --------------------------------------------------- */

    TIKU_I2C_I2CSA = addr;

    /* Transmitter mode, generate START */
    TIKU_I2C_CTLW0 |= UCTR | UCTXSTT;

    for (i = 0; i < tx_len; i++) {
        rc = i2c_wait_flag(UCTXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        TIKU_I2C_TXBUF = tx_buf[i];
    }

    /* Wait for last TX byte to finish */
    rc = i2c_wait_flag(UCTXIFG0);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }

    /* --- Repeated START into read phase -------------------------------- */

    /* Switch to receiver mode and issue repeated START */
    TIKU_I2C_CTLW0 &= ~UCTR;
    TIKU_I2C_CTLW0 |= UCTXSTT;

    if (rx_len == 1) {
        /* Single-byte read: STOP right after address phase */
        timeout = I2C_TIMEOUT;
        while (TIKU_I2C_CTLW0 & UCTXSTT) {
            if (--timeout == 0) {
                TIKU_I2C_CTLW0 |= UCTXSTP;
                return TIKU_I2C_ERR_TIMEOUT;
            }
        }
        TIKU_I2C_CTLW0 |= UCTXSTP;

        rc = i2c_wait_flag(UCRXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        rx_buf[0] = (uint8_t)TIKU_I2C_RXBUF;

        return i2c_wait_stop();
    }

    /* Multi-byte read */
    for (i = 0; i < rx_len; i++) {
        if (i == rx_len - 1) {
            TIKU_I2C_CTLW0 |= UCTXSTP;
        }

        rc = i2c_wait_flag(UCRXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        rx_buf[i] = (uint8_t)TIKU_I2C_RXBUF;
    }

    return i2c_wait_stop();
}

#endif /* TIKU_BOARD_I2C_BRW_100K */
