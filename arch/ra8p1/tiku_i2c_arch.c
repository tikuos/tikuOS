/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - RA8P1 I2C, unimplemented.
 *
 * No hardware backend yet. Every call fails cleanly so a caller learns the
 * bus is absent instead of reading zeros as data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_i2c_arch.h"

int tiku_i2c_arch_init(const tiku_i2c_config_t *config) {
    (void)config;
    return TIKU_I2C_ERR_PARAM;
}

void tiku_i2c_arch_close(void) {
}

int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len) {
    (void)addr; (void)buf; (void)len;
    return TIKU_I2C_ERR_PARAM;
}

int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len) {
    (void)addr; (void)buf; (void)len;
    return TIKU_I2C_ERR_PARAM;
}

int tiku_i2c_arch_probe(uint8_t addr) {
    (void)addr;
    return TIKU_I2C_ERR_PARAM;
}

int tiku_i2c_arch_write_read(uint8_t addr,
                             const uint8_t *tx_buf, uint16_t tx_len,
                             uint8_t *rx_buf,       uint16_t rx_len) {
    (void)addr; (void)tx_buf; (void)tx_len; (void)rx_buf; (void)rx_len;
    return TIKU_I2C_ERR_PARAM;
}
