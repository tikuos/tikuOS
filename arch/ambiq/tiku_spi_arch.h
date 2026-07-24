/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - Apollo 510 SPI driver interface
 *
 * Stub at this milestone (returns not-supported); a real am_hal_iom
 * backend lands with the peripheral pass.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_SPI_ARCH_H_
#define TIKU_AMBIQ_SPI_ARCH_H_

#include <interfaces/bus/tiku_spi_bus.h>

/**
 * @brief Initialize the SPI peripheral with the given configuration.
 *
 * Stub — returns a not-supported error code. A real am_hal_iom
 * (I/O Master) SPI backend will replace this in the peripheral pass.
 *
 * @param config  Pointer to the SPI configuration structure.
 * @return 0 on success, negative error code on failure.
 */
int     tiku_spi_arch_init(const tiku_spi_config_t *config);

/**
 * @brief Release the SPI peripheral and power it down.
 *
 * Stub — no-op at this milestone.
 */
void    tiku_spi_arch_close(void);

/**
 * @brief Transfer one byte over SPI (full-duplex).
 *
 * Transmits @p tx_byte and simultaneously captures the received byte.
 * Stub — returns 0xFF at this milestone.
 *
 * @param tx_byte  Byte to transmit.
 * @return Byte received during the transfer.
 */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);

/**
 * @brief Transmit a buffer over SPI (write-only).
 *
 * Stub — returns a not-supported error code.
 *
 * @param buf  Data buffer to transmit.
 * @param len  Number of bytes to write.
 * @return 0 on success, negative error code on failure.
 */
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);

/**
 * @brief Receive a buffer over SPI (read-only, transmits 0xFF).
 *
 * Stub — returns a not-supported error code.
 *
 * @param buf  Destination buffer for received bytes.
 * @param len  Number of bytes to read.
 * @return 0 on success, negative error code on failure.
 */
int     tiku_spi_arch_read(uint8_t *buf, uint16_t len);

/**
 * @brief Perform a simultaneous SPI write and read (full-duplex).
 *
 * Transmits @p len bytes from @p tx_buf while capturing @p len bytes
 * into @p rx_buf. Stub — returns a not-supported error code.
 *
 * @param tx_buf  Data to transmit.
 * @param rx_buf  Destination buffer for received bytes.
 * @param len     Number of bytes to transfer.
 * @return 0 on success, negative error code on failure.
 */
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_AMBIQ_SPI_ARCH_H_ */
