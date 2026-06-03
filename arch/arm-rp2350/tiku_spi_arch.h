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

int     tiku_spi_arch_init(const tiku_spi_config_t *config);
void    tiku_spi_arch_close(void);
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);
int     tiku_spi_arch_read (uint8_t *buf, uint16_t len);
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_RP2350_SPI_ARCH_H_ */
