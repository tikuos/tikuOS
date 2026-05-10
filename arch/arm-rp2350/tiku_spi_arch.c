/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - RP2350 SPI stub (PL022 driver TODO)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_spi_arch.h"

int tiku_spi_arch_init(const tiku_spi_config_t *config) {
    (void)config;
    return TIKU_SPI_OK;
}

void tiku_spi_arch_close(void) { /* nothing */ }

uint8_t tiku_spi_arch_transfer(uint8_t tx_byte) {
    (void)tx_byte;
    return 0xFFU;
}

int tiku_spi_arch_write(const uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    return TIKU_SPI_ERR_PARAM;
}

int tiku_spi_arch_read(uint8_t *buf, uint16_t len) {
    if (buf != (void *)0) {
        uint16_t i;
        for (i = 0; i < len; i++) {
            buf[i] = 0xFFU;
        }
    }
    return TIKU_SPI_ERR_PARAM;
}

int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len) {
    (void)tx_buf;
    if (rx_buf != (void *)0) {
        uint16_t i;
        for (i = 0; i < len; i++) {
            rx_buf[i] = 0xFFU;
        }
    }
    return TIKU_SPI_ERR_PARAM;
}
