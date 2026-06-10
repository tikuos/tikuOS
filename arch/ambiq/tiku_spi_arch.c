/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - Apollo 510 SPI driver (stub)
 *
 * Not yet supported. A real am_hal_iom backend lands with the peripheral
 * pass.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_spi_arch.h"

int tiku_spi_arch_init(const tiku_spi_config_t *config) {
    (void)config;
    return -1;
}

void tiku_spi_arch_close(void) {
}

uint8_t tiku_spi_arch_transfer(uint8_t tx_byte) {
    (void)tx_byte;
    return 0xFF;
}

int tiku_spi_arch_write(const uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    return -1;
}

int tiku_spi_arch_read(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    return -1;
}

int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len) {
    (void)tx_buf; (void)rx_buf; (void)len;
    return -1;
}
