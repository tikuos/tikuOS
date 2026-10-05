/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - STM32N6 SPI stub.
 *
 * This port has no SPI driver: tiku_spi_arch_transfer() returns 0xFF and
 * every other call returns TIKU_SPI_ERR_PARAM, moving no data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_SPI_ARCH_H_
#define TIKU_STM32N6_SPI_ARCH_H_

#include <interfaces/bus/tiku_spi_bus.h>

/** @brief Ignores @p config and returns TIKU_SPI_ERR_PARAM. */
int     tiku_spi_arch_init(const tiku_spi_config_t *config);

/** @brief Does nothing. */
void    tiku_spi_arch_close(void);

/** @brief Sends nothing and returns 0xFF, the idle level of MISO. */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);

/** @brief Sends nothing and returns TIKU_SPI_ERR_PARAM. */
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);

/** @brief Leaves @p buf untouched and returns TIKU_SPI_ERR_PARAM. */
int     tiku_spi_arch_read (uint8_t *buf, uint16_t len);

/**
 * @brief Sends nothing, leaves @p rx_buf untouched and returns
 *        TIKU_SPI_ERR_PARAM.
 */
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_STM32N6_SPI_ARCH_H_ */
