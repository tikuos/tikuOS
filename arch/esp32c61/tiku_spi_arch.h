/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - ESP32-C61 SPI contract.
 *
 * This port has no SPI driver: every call with a return code returns
 * TIKU_SPI_ERR_PARAM; tiku_spi_arch_transfer() returns 0xFF, the value an
 * idle MISO line reads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_SPI_ARCH_H_
#define TIKU_ESP32C61_SPI_ARCH_H_

#include <interfaces/bus/tiku_spi_bus.h>

/**
 * @brief Configures nothing.
 * @param config  Ignored
 * @return TIKU_SPI_ERR_PARAM
 */
int     tiku_spi_arch_init(const tiku_spi_config_t *config);

/** @brief Does nothing. */
void    tiku_spi_arch_close(void);

/** @brief Sends nothing. @param tx_byte  Ignored @return 0xFF */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);

/** @brief Sends nothing. @return TIKU_SPI_ERR_PARAM */
int     tiku_spi_arch_write(const uint8_t *buf, uint16_t len);

/** @brief Reads nothing into @p buf. @return TIKU_SPI_ERR_PARAM */
int     tiku_spi_arch_read (uint8_t *buf, uint16_t len);

/**
 * @brief Transfers nothing; @p rx_buf is left as it is.
 *
 * @return TIKU_SPI_ERR_PARAM
 */
int     tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                                 uint16_t len);

#endif /* TIKU_ESP32C61_SPI_ARCH_H_ */
