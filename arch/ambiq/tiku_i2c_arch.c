/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - Apollo 510 I2C driver (stub)
 *
 * Not yet supported. A real am_hal_iom backend lands with the peripheral
 * pass.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_i2c_arch.h"

int tiku_i2c_arch_init(const tiku_i2c_config_t *config) {
    (void)config;
    return -1;
}

void tiku_i2c_arch_close(void) {
}

int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len) {
    (void)addr; (void)buf; (void)len;
    return -1;
}

int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len) {
    (void)addr; (void)buf; (void)len;
    return -1;
}

int tiku_i2c_arch_write_read(uint8_t addr,
                             const uint8_t *tx_buf, uint16_t tx_len,
                             uint8_t *rx_buf, uint16_t rx_len) {
    (void)addr; (void)tx_buf; (void)tx_len; (void)rx_buf; (void)rx_len;
    return -1;
}
