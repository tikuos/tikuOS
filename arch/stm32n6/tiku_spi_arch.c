/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - STM32N6 SPI, unimplemented.
 *
 * No hardware backend yet. Every call fails cleanly so a caller learns the
 * bus is absent instead of reading zeros as data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_spi_arch.h"

int tiku_spi_arch_init(const tiku_spi_config_t *config) {
    (void)config;
    return TIKU_SPI_ERR_PARAM;
}

void tiku_spi_arch_close(void) {
}

uint8_t tiku_spi_arch_transfer(uint8_t tx_byte) {
    (void)tx_byte;
    return 0xFFU;   /* an idle MISO line reads high */
}

int tiku_spi_arch_write(const uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    return TIKU_SPI_ERR_PARAM;
}

int tiku_spi_arch_read(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    return TIKU_SPI_ERR_PARAM;
}

int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len) {
    (void)tx_buf; (void)rx_buf; (void)len;
    return TIKU_SPI_ERR_PARAM;
}
