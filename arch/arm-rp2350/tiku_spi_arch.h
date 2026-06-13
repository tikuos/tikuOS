/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - RP2350 SPI driver interface
 *
 * Drives the SPI0 PL022 controller (RP2350 datasheet §12.5). Master
 * mode, 8-bit frames, Motorola format. The driver auto-computes
 * SSPCPSR / SSPCR0.SCR from clk_peri and the requested baud, so the
 * same firmware works across all six supported clk_sys frequencies.
 * CS/SCK/MOSI/MISO pins are board-defined; see the per-board header
 * for the pin assignment.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_SPI_ARCH_H_
#define TIKU_RP2350_SPI_ARCH_H_

#include <interfaces/bus/tiku_spi_bus.h>

/**
 * @brief Initialize SPI0 (PL022) from the given board configuration.
 *
 * Takes SPI0 out of reset, configures SSPCPSR and SSPCR0.SCR from
 * clk_peri for the requested baud rate, selects Motorola frame format
 * with CPOL/CPHA from @p config, and applies pin function-selects.
 *
 * @param config  Bus configuration (baud, mode, pin assignments).
 * @return 0 on success, negative errno-style code on failure.
 */
int     tiku_spi_arch_init(const tiku_spi_config_t *config);

/**
 * @brief Disable SPI0 and release its GPIO pins.
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
 * @return         Byte received during the same clock cycle.
 */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);

/**
 * @brief Transmit @p len bytes, discarding received data.
 *
 * @param buf  Source byte array.
 * @param len  Number of bytes to send.
 * @return 0 on success, negative code on error.
 */
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);

/**
 * @brief Receive @p len bytes, sending 0xFF as dummy TX data.
 *
 * @param buf  Destination buffer for received bytes.
 * @param len  Number of bytes to receive.
 * @return 0 on success, negative code on error.
 */
int     tiku_spi_arch_read (uint8_t *buf, uint16_t len);

/**
 * @brief Transmit and receive @p len bytes simultaneously.
 *
 * @param tx_buf  Source data to transmit.
 * @param rx_buf  Destination for simultaneously received data.
 * @param len     Number of bytes to transfer.
 * @return 0 on success, negative code on error.
 */
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_RP2350_SPI_ARCH_H_ */
