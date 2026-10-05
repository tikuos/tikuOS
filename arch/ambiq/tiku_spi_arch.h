/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - Ambiq SPI master interface.
 *
 * The IOM6 master is built with TIKU_SPI_IOM_ENABLE (the EM9305 BLE build).
 * Without it, init and the buffer calls return -1 and a byte transfer 0xFF.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_SPI_ARCH_H_
#define TIKU_AMBIQ_SPI_ARCH_H_

#include <interfaces/bus/tiku_spi_bus.h>

/**
 * @brief Initialize the SPI peripheral with the given configuration.
 *
 * Powers IOM6, routes SCK/MOSI/MISO and sets the mode with full duplex.  The
 * bit_order and prescaler fields are ignored: MSB first at 16 MHz.
 *
 * @param config  Pointer to the SPI configuration structure.
 * @return 0 on success, -1 if @p config is NULL or the master is not built.
 */
int     tiku_spi_arch_init(const tiku_spi_config_t *config);

/** @brief Release the SPI peripheral and power it down. */
void    tiku_spi_arch_close(void);

/**
 * @brief Transfer one byte over SPI (full-duplex).
 *
 * Transmits @p tx_byte and simultaneously captures the received byte.
 *
 * @param tx_byte  Byte to transmit.
 * @return Byte received; 0xFF before init or without the master.
 */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);

/**
 * @brief Transmit a buffer over SPI (write-only).
 *
 * @param buf  Data buffer to transmit.
 * @param len  Number of bytes to write.
 * @return 0 on success, -1 on a NULL or empty buffer, before init or without
 *         the master.
 */
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);

/**
 * @brief Receive a buffer over SPI (read-only, transmits 0xFF).
 *
 * @param buf  Destination buffer for received bytes.
 * @param len  Number of bytes to read.
 * @return 0 on success, -1 on a NULL or empty buffer, before init or without
 *         the master.
 */
int     tiku_spi_arch_read(uint8_t *buf, uint16_t len);

/**
 * @brief Perform a simultaneous SPI write and read (full-duplex).
 *
 * Transmits @p len bytes from @p tx_buf while capturing @p len bytes
 * into @p rx_buf.
 *
 * @param tx_buf  Data to transmit.
 * @param rx_buf  Destination buffer for received bytes.
 * @param len     Number of bytes to transfer.
 * @return 0 on success, -1 on a NULL buffer or zero length, before init or
 *         without the master.
 */
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_AMBIQ_SPI_ARCH_H_ */
