/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_spi_arch.h - STM32F411RE SPI driver interface
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_SPI_ARCH_H_
#define TIKU_STM32F411_SPI_ARCH_H_

#include <interfaces/bus/tiku_spi_bus.h>

int     tiku_spi_arch_init(const tiku_spi_config_t *config);
void    tiku_spi_arch_close(void);
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);
int     tiku_spi_arch_read(uint8_t *buf, uint16_t len);
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_STM32F411_SPI_ARCH_H_ */
