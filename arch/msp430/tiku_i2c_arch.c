/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - I2C master driver for MSP430 eUSCI_B0
 *
 * Implements blocking I2C master transactions on the eUSCI_B0 peripheral.
 * Pin routing and clock prescaler values come from the board header via
 * TIKU_BOARD_I2C_* macros. All busy-wait loops are guarded by a timeout
 * counter to prevent infinite hangs on bus errors.
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

#include "tiku_i2c_arch.h"
#include "tiku.h"

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/** Busy-wait loop iteration limit to prevent infinite hangs. */
#define I2C_TIMEOUT     10000U

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Wait for a flag in UCB0IFG with timeout and NACK detection.
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
            while (UCB0CTLW0 & UCTXSTP) {
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
 * Clock source is SMCLK. The prescaler is selected from board macros
 * based on the requested speed mode:
 *   - Standard mode (100 kHz): TIKU_BOARD_I2C_BRW_100K
 *   - Fast mode (400 kHz):     TIKU_BOARD_I2C_BRW_400K
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

    /* Auto STOP when byte counter reaches UCB0TBCNT (disabled: manual) */
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
 * For a single-byte read the STOP must be issued right after the
 * address phase completes (before the first byte is clocked in).
 */
int
tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint16_t timeout;
    int rc;

    /* Set slave address */
    UCB0I2CSA = addr;

    /* Receiver mode, generate START + address */
    UCB0CTLW0 &= ~UCTR;
    UCB0CTLW0 |= UCTXSTT;

    if (len == 1) {
        /*
         * Single-byte read: must send STOP while START is still
         * pending so that only one byte is clocked.
         */
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
