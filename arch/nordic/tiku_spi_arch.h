/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - nRF54L SPI arch header (stub port)
 *
 * The SPI backend is a stub on this port (see tiku_spi_arch.c); a real SPIM
 * driver is a later phase.  Unlike adc/i2c/onewire (whose interface layers are
 * board-capability gated), interfaces/bus/tiku_spi_bus.c calls tiku_spi_arch_*
 * unconditionally, so these prototypes are declared here (mirroring the RP2350
 * arch header) to keep the call sites warning-free.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_SPI_ARCH_H_
#define TIKU_NORDIC_SPI_ARCH_H_

#include <stdint.h>
#include <interfaces/bus/tiku_spi_bus.h>

/** @brief Initialise the SPI master (stub: returns failure). */
int     tiku_spi_arch_init(const tiku_spi_config_t *config);

/** @brief Close/deinit the SPI master (stub: no-op). */
void    tiku_spi_arch_close(void);

/** @brief Full-duplex one-byte transfer (stub: returns 0xFF idle level). */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);

/** @brief Write a buffer (stub: returns failure). */
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);

/** @brief Read a buffer (stub: returns failure, buffer untouched). */
int     tiku_spi_arch_read(uint8_t *buf, uint16_t len);

/** @brief Simultaneous write+read (stub: returns failure). */
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_NORDIC_SPI_ARCH_H_ */
