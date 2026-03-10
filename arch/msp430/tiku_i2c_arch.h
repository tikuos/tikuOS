/*
 * Tiku Operating System
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
 * Configures eUSCI_B0 for I2C master mode with the clock speed
 * specified in @p config. Sets up SDA/SCL pins via board macros.
 *
 * @param config  Pointer to I2C configuration
 * @return TIKU_I2C_OK on success, negative error code on failure
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
 * @param addr  7-bit slave address
 * @param buf   Data to transmit
 * @param len   Number of bytes
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific I2C read.
 *
 * @param addr  7-bit slave address
 * @param buf   Buffer for received data
 * @param len   Number of bytes to read
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific combined write-then-read.
 *
 * Uses a repeated START between write and read phases.
 *
 * @param addr    7-bit slave address
 * @param tx_buf  Data to transmit
 * @param tx_len  Transmit length
 * @param rx_buf  Buffer for received data
 * @param rx_len  Receive length
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
int tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf, uint16_t rx_len);

#endif /* TIKU_I2C_ARCH_H_ */
