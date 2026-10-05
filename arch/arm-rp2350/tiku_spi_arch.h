/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - RP2350 SPI driver interface.
 *
 * Drives the SPI0 PL022 in master mode with 8-bit Motorola frames.  CPSR and
 * SCR come from the configured prescaler, a divider of clk_peri.  Pins are
 * board-defined.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_SPI_ARCH_H_
#define TIKU_RP2350_SPI_ARCH_H_

#include <interfaces/bus/tiku_spi_bus.h>

/**
 * @brief Initialize SPI0 (PL022) from the given configuration.
 *
 * Takes SPI0 out of reset, muxes the board's SCK/MOSI/MISO pins, sets SSPCPSR
 * and SSPCR0.SCR from the prescaler and CPOL/CPHA from the mode, then enables
 * the controller.
 *
 * @param config  Bus configuration (mode, bit order, prescaler).
 * @return TIKU_SPI_OK, or TIKU_SPI_ERR_PARAM for a NULL config, a mode above
 *         3 or LSB-first bit order.
 */
int     tiku_spi_arch_init(const tiku_spi_config_t *config);

/**
 * @brief Wait for the bus to go idle, then disable SPI0.
 *
 * The pins stay on the SPI function.
 */
void    tiku_spi_arch_close(void);

/**
 * @brief Send one byte and return the simultaneously received byte.
 *
 * Blocks until the TX FIFO has room, writes @p tx_byte, then waits
 * for the RX FIFO to become non-empty and returns the received byte.
 * The SSP shifts bits simultaneously in both directions.
 *
 * @param tx_byte  Byte to transmit.
 * @return         Byte received during the same eight clocks; 0xFF on
 *                 timeout or when SPI0 is not initialised.
 */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);

/**
 * @brief Transmit @p len bytes, discarding received data.
 *
 * @param buf  Source byte array.
 * @param len  Number of bytes to send.
 * @return TIKU_SPI_OK, TIKU_SPI_ERR_PARAM or TIKU_SPI_ERR_TIMEOUT.
 */
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);

/**
 * @brief Receive @p len bytes, sending 0xFF as dummy TX data.
 *
 * @param buf  Destination buffer for received bytes.
 * @param len  Number of bytes to receive.
 * @return TIKU_SPI_OK, TIKU_SPI_ERR_PARAM or TIKU_SPI_ERR_TIMEOUT.
 */
int     tiku_spi_arch_read (uint8_t *buf, uint16_t len);

/**
 * @brief Transmit and receive @p len bytes simultaneously.
 *
 * @param tx_buf  Source data to transmit.
 * @param rx_buf  Destination for simultaneously received data.
 * @param len     Number of bytes to transfer.
 * @return TIKU_SPI_OK, TIKU_SPI_ERR_PARAM or TIKU_SPI_ERR_TIMEOUT.
 */
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_RP2350_SPI_ARCH_H_ */
