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

/**
 * @brief Initialize the I2C peripheral (stub)
 *
 * Not yet implemented. A real backend brings up an am_hal_iom instance
 * in I2C master mode and configures the bus speed from @p config.
 *
 * @param config  Pointer to the I2C configuration struct
 * @return -1 always (not supported yet)
 */
int tiku_i2c_arch_init(const tiku_i2c_config_t *config) {
    (void)config;
    return -1;
}

/**
 * @brief Release the I2C peripheral (stub)
 *
 * Not yet implemented. A real backend calls am_hal_iom_disable() and
 * am_hal_iom_deinitialize().
 */
void tiku_i2c_arch_close(void) {
}

/**
 * @brief Write bytes to an I2C device (stub)
 *
 * Not yet implemented.
 *
 * @param addr  7-bit I2C device address
 * @param buf   Source buffer in SRAM
 * @param len   Number of bytes to write
 * @return -1 always (not supported yet)
 */
int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len) {
    (void)addr; (void)buf; (void)len;
    return -1;
}

/**
 * @brief Read bytes from an I2C device (stub)
 *
 * Not yet implemented.
 *
 * @param addr  7-bit I2C device address
 * @param buf   Destination buffer in SRAM
 * @param len   Number of bytes to read
 * @return -1 always (not supported yet)
 */
int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len) {
    (void)addr; (void)buf; (void)len;
    return -1;
}

/**
 * @brief Write then read an I2C device in a single transaction (stub)
 *
 * Not yet implemented. A real backend performs a combined write/read
 * with a repeated START between the two phases.
 *
 * @param addr    7-bit I2C device address
 * @param tx_buf  Source buffer for the write phase
 * @param tx_len  Number of bytes to write
 * @param rx_buf  Destination buffer for the read phase
 * @param rx_len  Number of bytes to read
 * @return -1 always (not supported yet)
 */
int tiku_i2c_arch_write_read(uint8_t addr,
                             const uint8_t *tx_buf, uint16_t tx_len,
                             uint8_t *rx_buf, uint16_t rx_len) {
    (void)addr; (void)tx_buf; (void)tx_len; (void)rx_buf; (void)rx_len;
    return -1;
}
