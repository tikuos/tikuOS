/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - RA8P1 SPI contract.
 *
 * No backend on this port: the calls exist so the kernel links.  Every call
 * with an error channel fails; tiku_spi_arch_transfer() has none and returns
 * 0xFF, the value an idle MISO line reads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_SPI_ARCH_H_
#define TIKU_RA8P1_SPI_ARCH_H_

#include <interfaces/bus/tiku_spi_bus.h>

/** @brief Configure a bus. @param config  Requested settings @return Error */
int     tiku_spi_arch_init(const tiku_spi_config_t *config);

/** @brief Release the bus. */
void    tiku_spi_arch_close(void);

/** @brief Exchange one byte. @param tx_byte  Byte to send @return Byte read */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);

/** @brief Send bytes. @param buf  Bytes @param len  Length @return Error */
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);

/** @brief Receive bytes. @param buf  Destination @param len  Length @return Error */
int     tiku_spi_arch_read (uint8_t *buf, uint16_t len);

/**
 * @brief Full-duplex transfer of equal-length buffers.
 *
 * @param tx_buf  Bytes to send
 * @param rx_buf  Receives the bytes read
 * @param len     Length of both buffers
 * @return TIKU_SPI_OK, or an error
 */
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_RA8P1_SPI_ARCH_H_ */
