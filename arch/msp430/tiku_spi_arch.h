/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - SPI master driver for MSP430 eUSCI_A (architecture layer)
 *
 * Declares the architecture-specific SPI functions implemented by
 * tiku_spi_arch.c using the eUSCI_A1 peripheral. These are called
 * by the platform-independent bus layer (interfaces/bus/tiku_spi_bus.c).
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

#ifndef TIKU_SPI_ARCH_H_
#define TIKU_SPI_ARCH_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <interfaces/bus/tiku_spi_bus.h>

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Architecture-specific SPI initialization.
 *
 * Configures eUSCI_A1 for 3-pin SPI master mode with the clock
 * polarity, phase, bit order, and prescaler specified in @p config.
 * Sets up SCLK/SIMO/SOMI pins via board macros.
 *
 * @param config  Pointer to SPI configuration
 * @return TIKU_SPI_OK on success, negative error code on failure
 */
int tiku_spi_arch_init(const tiku_spi_config_t *config);

/**
 * @brief Architecture-specific SPI shutdown.
 *
 * Places eUSCI_A1 in software reset.
 */
void tiku_spi_arch_close(void);

/**
 * @brief Architecture-specific single-byte full-duplex transfer.
 *
 * @param tx_byte  Byte to transmit
 * @return Byte received from the slave
 */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);

/**
 * @brief Architecture-specific SPI write (RX discarded).
 *
 * @param buf  Data to transmit
 * @param len  Number of bytes
 * @return TIKU_SPI_OK on success, negative error code on failure
 */
int tiku_spi_arch_write(const uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific SPI read (TX sends 0xFF).
 *
 * @param buf  Buffer for received data
 * @param len  Number of bytes to read
 * @return TIKU_SPI_OK on success, negative error code on failure
 */
int tiku_spi_arch_read(uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific full-duplex bulk transfer.
 *
 * @param tx_buf  Data to transmit
 * @param rx_buf  Buffer for received data
 * @param len     Number of bytes
 * @return TIKU_SPI_OK on success, negative error code on failure
 */
int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                              uint16_t len);

#endif /* TIKU_SPI_ARCH_H_ */
