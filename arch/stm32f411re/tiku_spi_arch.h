/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_spi_arch.h - SPI master driver for STM32F411RE
 *
 * Declares the architecture-specific SPI functions implemented by
 * tiku_spi_arch.c using the SPI1 peripheral. The STM32 backend runs
 * as a blocking SPI master in 2-line full-duplex mode with 8-bit
 * frames and Motorola formatting. Chip select remains managed by the
 * caller via GPIO.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_SPI_ARCH_H_
#define TIKU_STM32F411_SPI_ARCH_H_

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
 * Configures SPI1 for 2-line full-duplex master operation with
 * 8-bit frames, Motorola format, the requested clock mode, and the
 * requested bit order. SCK/MISO/MOSI pins are selected from the
 * active board header.
 *
 * @param config  Pointer to SPI configuration
 * @return TIKU_SPI_OK on success, negative error code on failure
 */
int tiku_spi_arch_init(const tiku_spi_config_t *config);

/**
 * @brief Architecture-specific SPI shutdown.
 *
 * Waits for the peripheral to go idle and then disables SPI1.
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

#endif /* TIKU_STM32F411_SPI_ARCH_H_ */
