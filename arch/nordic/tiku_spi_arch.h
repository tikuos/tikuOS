/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - nRF54L SPI arch header.
 *
 * Declares the blocking SPIM (EasyDMA) master that the SPI bus interface
 * calls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_SPI_ARCH_H_
#define TIKU_NORDIC_SPI_ARCH_H_

#include <stdint.h>
#include <interfaces/bus/tiku_spi_bus.h>

/**
 * @brief Initialise the SPIM master: pins, mode, bit order, and SCK =
 *        16 MHz / divisor.
 *
 * The divisor is config->prescaler, clamped to an even 2..126; 0 or 1
 * selects 16 (1 MHz).
 *
 * @return TIKU_SPI_OK, or TIKU_SPI_ERR_PARAM on a NULL config or a mode
 *         above 3
 */
int     tiku_spi_arch_init(const tiku_spi_config_t *config);

/** @brief Disable the SPIM and disconnect its pins. */
void    tiku_spi_arch_close(void);

/**
 * @brief Exchange one byte.
 *
 * @return the received byte, or 0xFF if not initialised or on a timeout
 */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);

/**
 * @brief Write @p len bytes, discarding the received ones; @p buf may be in
 *        RRAM.
 *
 * @return TIKU_SPI_OK, TIKU_SPI_ERR_PARAM if not initialised or @p buf is
 *         NULL with @p len > 0, TIKU_SPI_ERR_TIMEOUT on a poll timeout
 */
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);

/**
 * @brief Read @p len bytes, clocking out 0xFF.
 *
 * @note @p buf must be in on-chip RAM.
 * @return TIKU_SPI_OK, TIKU_SPI_ERR_PARAM if not initialised or @p buf is
 *         NULL or not RAM, TIKU_SPI_ERR_TIMEOUT on a poll timeout
 */
int     tiku_spi_arch_read(uint8_t *buf, uint16_t len);

/**
 * @brief Exchange @p len bytes: send @p tx_buf while receiving into @p rx_buf.
 *
 * @note @p rx_buf must be in on-chip RAM; @p tx_buf may be in RRAM.
 * @return TIKU_SPI_OK, TIKU_SPI_ERR_PARAM if not initialised, a buffer is
 *         NULL or @p rx_buf is not RAM, TIKU_SPI_ERR_TIMEOUT on a timeout
 */
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_NORDIC_SPI_ARCH_H_ */
