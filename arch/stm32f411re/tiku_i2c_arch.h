/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_i2c_arch.h - STM32F411RE I2C driver interface
 *
 * Declares the architecture-specific I2C functions implemented by
 * tiku_i2c_arch.c using the I2C1 peripheral. The STM32 backend runs as
 * a blocking master on PB8/PB9 with external pull-up resistors and
 * supports write, read, and repeated-START write-then-read transfers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_I2C_ARCH_H_
#define TIKU_STM32F411_I2C_ARCH_H_

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
 * Configures I2C1 for polling-only blocking master transfers at the
 * requested bus speed and prepares PB8/PB9 for AF4 open-drain operation
 * with no internal pull-ups. Timing is derived from the live APB1 clock
 * for standard-mode (100 kHz) or fast-mode (400 kHz) operation.
 *
 * @param config  Pointer to I2C configuration
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);

/**
 * @brief Architecture-specific I2C shutdown.
 *
 * Disables the I2C1 peripheral and marks the backend unavailable for
 * further transfers until reinitialized.
 */
void tiku_i2c_arch_close(void);

/**
 * @brief Architecture-specific I2C write.
 *
 * Issues a blocking master write transaction to the 7-bit slave
 * address in @p addr and transmits @p len bytes from @p buf. The
 * backend waits for the bus to become idle, checks STM32 fault bits,
 * and generates STOP on completion.
 *
 * @param addr  7-bit slave address
 * @param buf   Data to transmit
 * @param len   Number of bytes
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific I2C read.
 *
 * Issues a blocking master read transaction to the 7-bit slave address
 * in @p addr and stores @p len received bytes in @p buf. The backend
 * follows the STM32F4-specific 1-byte, 2-byte, and multi-byte receive
 * sequences and finalizes the transfer with STOP.
 *
 * @param addr  7-bit slave address
 * @param buf   Buffer for received data
 * @param len   Number of bytes to read
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int  tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific combined write-then-read.
 *
 * Executes a blocking write phase followed by a repeated START and a
 * read phase against the same 7-bit slave address, reusing the common
 * STM32 master transmit and receive paths.
 *
 * @param addr    7-bit slave address
 * @param tx_buf  Data to transmit
 * @param tx_len  Transmit length
 * @param rx_buf  Buffer for received data
 * @param rx_len  Receive length
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf, uint16_t rx_len);

#endif /* TIKU_STM32F411_I2C_ARCH_H_ */
