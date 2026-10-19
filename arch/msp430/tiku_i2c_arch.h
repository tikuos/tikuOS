/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - I2C master driver for MSP430 eUSCI_B (architecture layer)
 *
 * Declares the architecture-specific I2C functions implemented by
 * tiku_i2c_arch.c using the eUSCI_B0 peripheral. These are called
 * by the platform-independent bus layer (interfaces/bus/tiku_i2c_bus.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_I2C_ARCH_H_
#define TIKU_I2C_ARCH_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <interfaces/bus/tiku_i2c_bus.h>

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Architecture-specific I2C initialization.
 *
 * Configures eUSCI_B0 for I2C master mode from SMCLK and sets up SDA/SCL
 * pins via board macros.  TIKU_I2C_SPEED_FAST takes the board's 400 kHz
 * prescaler; any other speed takes its 100 kHz one.
 *
 * @param config  Pointer to I2C configuration
 * @return TIKU_I2C_OK
 */
int tiku_i2c_arch_init(const tiku_i2c_config_t *config);

/**
 * @brief Architecture-specific I2C shutdown.
 *
 * Places eUSCI_B0 in software reset.
 */
void tiku_i2c_arch_close(void);

/**
 * @brief Architecture-specific I2C write.
 *
 * Sends START, the address with the write bit, @p len bytes and STOP.
 *
 * @param addr  7-bit slave address
 * @param buf   Data to transmit
 * @param len   Number of bytes
 * @return TIKU_I2C_OK, TIKU_I2C_ERR_NACK or TIKU_I2C_ERR_TIMEOUT
 */
int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific I2C read.
 *
 * Sends START and the address with the read bit, receives @p len bytes,
 * NACKs the last one and sends STOP.
 *
 * @param addr  7-bit slave address
 * @param buf   Buffer for received data
 * @param len   Number of bytes to read
 * @return TIKU_I2C_OK, TIKU_I2C_ERR_NACK or TIKU_I2C_ERR_TIMEOUT
 */
int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific address probe (bus-scan presence check).
 *
 * Sends START and the address with the write bit, checks the ACK and sends
 * STOP; no data byte moves.  The `i2c scan` command uses it; the bus layer
 * refuses zero-length writes.
 *
 * @param addr  7-bit slave address (unshifted)
 * @return TIKU_I2C_OK if acknowledged, TIKU_I2C_ERR_NACK if not, or
 *         TIKU_I2C_ERR_TIMEOUT when the START or the STOP does not complete
 */
int tiku_i2c_arch_probe(uint8_t addr);

/**
 * @brief Architecture-specific combined write-then-read.
 *
 * Sends START, the address with the write bit and @p tx_len bytes, then a
 * repeated START, the address with the read bit, @p rx_len bytes and STOP.
 *
 * @param addr    7-bit slave address
 * @param tx_buf  Data to transmit
 * @param tx_len  Transmit length
 * @param rx_buf  Buffer for received data
 * @param rx_len  Receive length
 * @return TIKU_I2C_OK, TIKU_I2C_ERR_NACK or TIKU_I2C_ERR_TIMEOUT
 */
int tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf, uint16_t rx_len);

#endif /* TIKU_I2C_ARCH_H_ */
