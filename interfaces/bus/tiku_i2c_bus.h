/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_bus.h - Platform-independent I2C bus interface
 *
 * Provides a portable I2C master API for communicating with slave devices.
 * Supports standard (100 kHz) and fast (400 kHz) modes. All operations
 * are synchronous (blocking). The underlying hardware is accessed through
 * the architecture-specific layer (arch/msp430/tiku_i2c_arch.c).
 *
 * Typical usage:
 *   tiku_i2c_config_t cfg = { .speed = TIKU_I2C_SPEED_STANDARD };
 *   tiku_i2c_init(&cfg);
 *   tiku_i2c_write(0x48, data, 2);
 *   tiku_i2c_write_read(0x48, &reg, 1, buf, 2);
 *   tiku_i2c_close();
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

#ifndef TIKU_I2C_BUS_H_
#define TIKU_I2C_BUS_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                      */
/*---------------------------------------------------------------------------*/

/** @defgroup TIKU_I2C_SPEED I2C Speed Modes
 * @{ */
#define TIKU_I2C_SPEED_STANDARD     0   /**< 100 kHz SCL */
#define TIKU_I2C_SPEED_FAST         1   /**< 400 kHz SCL */
/** @} */

/** @defgroup TIKU_I2C_STATUS I2C Status Codes
 * @{ */
#define TIKU_I2C_OK                 0   /**< Operation succeeded */
#define TIKU_I2C_ERR_NACK         (-1)  /**< Slave did not acknowledge */
#define TIKU_I2C_ERR_TIMEOUT      (-2)  /**< Bus operation timed out */
#define TIKU_I2C_ERR_BUSY         (-3)  /**< Bus is busy */
#define TIKU_I2C_ERR_PARAM        (-4)  /**< Invalid parameter */
/** @} */

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief I2C bus configuration.
 */
typedef struct tiku_i2c_config {
    uint8_t speed;  /**< TIKU_I2C_SPEED_STANDARD or TIKU_I2C_SPEED_FAST */
} tiku_i2c_config_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the I2C bus in master mode.
 *
 * Configures the underlying hardware peripheral (eUSCI_B on MSP430)
 * for I2C master operation at the requested clock speed.
 *
 * @param config  Pointer to configuration structure
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int tiku_i2c_init(const tiku_i2c_config_t *config);

/**
 * @brief Shut down the I2C bus.
 *
 * Places the peripheral in reset and releases the I/O pins.
 */
void tiku_i2c_close(void);

/**
 * @brief Write bytes to an I2C slave.
 *
 * Sends START, slave address + W, data bytes, then STOP.
 *
 * @param addr  7-bit slave address (unshifted)
 * @param buf   Data to transmit
 * @param len   Number of bytes to transmit (must be >= 1)
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int tiku_i2c_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Read bytes from an I2C slave.
 *
 * Sends START, slave address + R, clocks in data bytes, then STOP.
 * A NACK is sent before STOP to signal end of transfer.
 *
 * @param addr  7-bit slave address (unshifted)
 * @param buf   Buffer to store received data
 * @param len   Number of bytes to read (must be >= 1)
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int tiku_i2c_read(uint8_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief Combined write-then-read (repeated START) transaction.
 *
 * Performs: START, addr+W, tx_buf, repeated-START, addr+R, rx_buf, STOP.
 * This is the standard pattern for reading registers from I2C devices.
 *
 * @param addr    7-bit slave address (unshifted)
 * @param tx_buf  Data to transmit (e.g. register address)
 * @param tx_len  Number of bytes to transmit (must be >= 1)
 * @param rx_buf  Buffer to store received data
 * @param rx_len  Number of bytes to read (must be >= 1)
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int tiku_i2c_write_read(uint8_t addr,
                         const uint8_t *tx_buf, uint16_t tx_len,
                         uint8_t *rx_buf, uint16_t rx_len);

#endif /* TIKU_I2C_BUS_H_ */
