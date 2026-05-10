/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - RP2350 I2C stub
 *
 * Real I2C support requires programming the DW_apb_i2c block at
 * 0x40090000 (I2C0) / 0x40098000 (I2C1) — slated for a later port.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_i2c_arch.h"

int  tiku_i2c_arch_init(const tiku_i2c_config_t *config) {
    (void)config;
    return TIKU_I2C_OK;
}

void tiku_i2c_arch_close(void) { /* nothing */ }

int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len) {
    (void)addr; (void)buf; (void)len;
    return TIKU_I2C_ERR_NACK;       /* "no slave responded" — sensible stub */
}

int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len) {
    (void)addr; (void)buf; (void)len;
    return TIKU_I2C_ERR_NACK;
}

int tiku_i2c_arch_write_read(uint8_t addr,
                             const uint8_t *tx_buf, uint16_t tx_len,
                             uint8_t *rx_buf,       uint16_t rx_len) {
    (void)addr; (void)tx_buf; (void)tx_len;
    (void)rx_buf; (void)rx_len;
    return TIKU_I2C_ERR_NACK;
}
