/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - I2C master driver for MSP430 eUSCI_B0.
 *
 * Blocking master transactions, with pin routing and clock prescaler taken
 * from the board header's TIKU_BOARD_I2C_* macros.  Every busy-wait gives up
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
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/** Busy-wait iteration limit. */
#define I2C_TIMEOUT     10000U

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Wait for a flag in UCB0IFG with timeout and NACK detection.
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

    while (!(UCB0IFG & flag)) {
        if (UCB0IFG & UCNACKIFG) {
            UCB0IFG &= ~UCNACKIFG;
            UCB0CTLW0 |= UCTXSTP;
            timeout = I2C_TIMEOUT;
            while ((UCB0CTLW0 & UCTXSTP) && --timeout != 0) {
                /* wait for STOP to complete */
            }
            return TIKU_I2C_ERR_NACK;
        }
        if (--timeout == 0) {
            UCB0CTLW0 |= UCTXSTP;
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

    while (UCB0CTLW0 & UCTXSTP) {
        if (--timeout == 0) {
            return TIKU_I2C_ERR_TIMEOUT;
        }
    }

    return TIKU_I2C_OK;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize eUSCI_B0 for I2C master operation.
 *
 * Clocked from SMCLK, with the prescaler taken from the board's macro for
 * the requested speed: 100 kHz standard or 400 kHz fast.
 */
int
tiku_i2c_arch_init(const tiku_i2c_config_t *config)
{
    /* Select I2C function on board-specific SDA/SCL pins */
    TIKU_BOARD_I2C_PINS_INIT();

    /* Put eUSCI_B0 in reset before configuration */
    UCB0CTLW0 = UCSWRST;

    /* I2C master mode, synchronous, SMCLK source */
    UCB0CTLW0 |= UCMODE_3 | UCMST | UCSSEL__SMCLK | UCSYNC;

    /* Set clock prescaler for requested speed */
    if (config->speed == TIKU_I2C_SPEED_FAST) {
        UCB0BRW = TIKU_BOARD_I2C_BRW_400K;
    } else {
        UCB0BRW = TIKU_BOARD_I2C_BRW_100K;
    }

    /* UCASTP_0: no automatic STOP; the driver sends STOP itself */
    UCB0CTLW1 = UCASTP_0;

    /* Release from reset — I2C is now active */
    UCB0CTLW0 &= ~UCSWRST;

    I2C_PRINTF("init: speed=%s\n",
               config->speed == TIKU_I2C_SPEED_FAST ? "400k" : "100k");

    return TIKU_I2C_OK;
}

/**
 * @brief Place eUSCI_B0 in software reset (I2C disabled).
 */
void
tiku_i2c_arch_close(void)
{
    UCB0CTLW0 |= UCSWRST;
    I2C_PRINTF("close\n");
}

/**
 * @brief Transmit bytes to a slave device.
 *
 * Sequence: START → addr+W → data[0..len-1] → STOP
 */
int
tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    int rc;

    /* Clear stale interrupt flags from any previous transaction */
    UCB0IFG = 0;

    /* Set slave address */
    UCB0I2CSA = addr;

    /* Transmitter mode, generate START + address */
    UCB0CTLW0 |= UCTR | UCTXSTT;

    for (i = 0; i < len; i++) {
        /* Wait for TX buffer ready */
        rc = i2c_wait_flag(UCTXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }

        /* Load next byte */
        UCB0TXBUF = buf[i];
    }

    /* Wait for final byte to shift out */
    rc = i2c_wait_flag(UCTXIFG0);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }

    /* Generate STOP */
    UCB0CTLW0 |= UCTXSTP;

    return i2c_wait_stop();
}

/**
 * @brief Receive bytes from a slave device.
 *
 * Sequence: START → addr+R → data[0..len-1] → NACK → STOP
 *
 * For a single-byte read, STOP is set as soon as the address phase ends
 * (UCTXSTT clears), while that byte is being received.
 */
int
tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint16_t timeout;
    int rc;

    /* Clear stale interrupt flags from any previous transaction */
    UCB0IFG = 0;

    /* Set slave address */
    UCB0I2CSA = addr;

    /* Receiver mode, generate START + address */
    UCB0CTLW0 &= ~UCTR;
    UCB0CTLW0 |= UCTXSTT;

    if (len == 1) {
        /*
         * Single-byte read: STOP goes out as soon as UCTXSTT clears,
         * while the byte is being received, so only one byte is clocked.
         */
        timeout = I2C_TIMEOUT;
        while ((UCB0CTLW0 & UCTXSTT) && !(UCB0IFG & UCNACKIFG)) {
            if (--timeout == 0) {
                UCB0CTLW0 |= UCTXSTP;
                return TIKU_I2C_ERR_TIMEOUT;
            }
        }
        UCB0CTLW0 |= UCTXSTP;

        rc = i2c_wait_flag(UCRXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        buf[0] = (uint8_t)UCB0RXBUF;

        return i2c_wait_stop();
    }

    /* Multi-byte read */
    for (i = 0; i < len; i++) {
        /* Issue STOP before reading the last byte */
        if (i == len - 1) {
            UCB0CTLW0 |= UCTXSTP;
        }

        rc = i2c_wait_flag(UCRXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        buf[i] = (uint8_t)UCB0RXBUF;
    }

    return i2c_wait_stop();
}

/**
 * @brief Probe a slave address (presence check, no data transferred).
 *
 * START, address with the write bit, ACK check, STOP; no data byte is sent.
 * A NACK sends STOP and returns TIKU_I2C_ERR_NACK.
 */
int
tiku_i2c_arch_probe(uint8_t addr)
{
    int rc;

    /* Clear stale interrupt flags from any previous transaction */
    UCB0IFG = 0;

    /* Set slave address */
    UCB0I2CSA = addr;

    /* Transmitter mode, generate START + address (no data follows) */
    UCB0CTLW0 |= UCTR | UCTXSTT;

    {
        uint16_t timeout = I2C_TIMEOUT;
        while ((UCB0CTLW0 & UCTXSTT) && !(UCB0IFG & UCNACKIFG)) {
            if (--timeout == 0u) {
                UCB0CTLW0 |= UCTXSTP;
                return TIKU_I2C_ERR_TIMEOUT;
            }
        }
    }
    rc = (UCB0IFG & UCNACKIFG) ? TIKU_I2C_ERR_NACK : TIKU_I2C_OK;
    UCB0CTLW0 |= UCTXSTP;
    if (i2c_wait_stop() != TIKU_I2C_OK) {
        return rc == TIKU_I2C_ERR_NACK ? rc : TIKU_I2C_ERR_TIMEOUT;
    }
    if (UCB0IFG & UCNACKIFG) {
        rc = TIKU_I2C_ERR_NACK;
    }
    return rc;
}

/**
 * @brief Combined write-then-read with repeated START.
 *
 * Sequence: START → addr+W → tx_data → rSTART → addr+R → rx_data → STOP
 *
 * This is the standard register-read pattern used by most I2C sensors
 * and peripherals: write the register address, then read back data.
 */
int
tiku_i2c_arch_write_read(uint8_t addr,
                          const uint8_t *tx_buf, uint16_t tx_len,
                          uint8_t *rx_buf, uint16_t rx_len)
{
    uint16_t i;
    uint16_t timeout;
    int rc;

    /* Clear stale interrupt flags from any previous transaction */
    UCB0IFG = 0;

    /* --- Write phase --------------------------------------------------- */

    UCB0I2CSA = addr;

    /* Transmitter mode, generate START */
    UCB0CTLW0 |= UCTR | UCTXSTT;

    for (i = 0; i < tx_len; i++) {
        rc = i2c_wait_flag(UCTXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        UCB0TXBUF = tx_buf[i];
    }

    /* Wait for last TX byte to finish */
    rc = i2c_wait_flag(UCTXIFG0);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }

    /* --- Repeated START into read phase -------------------------------- */

    /* Switch to receiver mode and issue repeated START */
    UCB0CTLW0 &= ~UCTR;
    UCB0CTLW0 |= UCTXSTT;

    if (rx_len == 1) {
        /* Single-byte read: STOP right after address phase */
        timeout = I2C_TIMEOUT;
        while (UCB0CTLW0 & UCTXSTT) {
            if (--timeout == 0) {
                UCB0CTLW0 |= UCTXSTP;
                return TIKU_I2C_ERR_TIMEOUT;
            }
        }
        UCB0CTLW0 |= UCTXSTP;

        rc = i2c_wait_flag(UCRXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        rx_buf[0] = (uint8_t)UCB0RXBUF;

        return i2c_wait_stop();
    }

    /* Multi-byte read */
    for (i = 0; i < rx_len; i++) {
        if (i == rx_len - 1) {
            UCB0CTLW0 |= UCTXSTP;
        }

        rc = i2c_wait_flag(UCRXIFG0);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        rx_buf[i] = (uint8_t)UCB0RXBUF;
    }

    return i2c_wait_stop();
}

#endif /* TIKU_BOARD_I2C_BRW_100K */
