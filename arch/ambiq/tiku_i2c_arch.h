/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - Ambiq I2C driver interface (stub)
 *
 * This port has no I2C driver: every call that returns a status returns -1
 * (TIKU_I2C_ERR_NACK) and no call touches the hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_I2C_ARCH_H_
#define TIKU_AMBIQ_I2C_ARCH_H_

#include <interfaces/bus/tiku_i2c_bus.h>

/**
 * @brief Initialize the I2C peripheral; fails on every call on this port.
 *
 * This port has no I2C driver: the call touches no hardware.
 *
 * @param config  Pointer to the I2C configuration structure.
 * @return -1 (TIKU_I2C_ERR_NACK).
 */
int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);

/**
 * @brief Release the I2C peripheral; does nothing on this port.
 */
void tiku_i2c_arch_close(void);

/**
 * @brief Write to an I2C device; fails on every call on this port.
 *
 * This port has no I2C driver: the call touches no hardware.
 *
 * @param addr  7-bit I2C target address (unshifted).
 * @param buf   Data buffer to transmit.
 * @param len   Number of bytes to write.
 * @return -1 (TIKU_I2C_ERR_NACK).
 */
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Read from an I2C device; fails on every call on this port.
 *
 * This port has no I2C driver: the call touches no hardware and leaves
 * @p buf unwritten.
 *
 * @param addr  7-bit I2C target address (unshifted).
 * @param buf   Destination buffer for received bytes.
 * @param len   Number of bytes to read.
 * @return -1 (TIKU_I2C_ERR_NACK).
 */
int  tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief Probe an I2C address for an acknowledge; fails on every call here.
 *
 * `i2c scan` calls it for each address, since the bus layer rejects a
 * zero-length write.  This port has no I2C driver: every address returns
 * -1, so a scan finds no device.
 *
 * @param addr  7-bit slave address (unshifted).
 * @return -1 (TIKU_I2C_ERR_NACK).
 */
int  tiku_i2c_arch_probe(uint8_t addr);

/**
 * @brief Write then read one device with a repeated START; fails here.
 *
 * This port has no I2C driver: the call touches no hardware and leaves
 * @p rx_buf unwritten.
 *
 * @param addr    7-bit I2C target address (unshifted).
 * @param tx_buf  Data buffer to transmit in the write phase.
 * @param tx_len  Number of bytes to write.
 * @param rx_buf  Destination buffer for the read phase.
 * @param rx_len  Number of bytes to read.
 * @return -1 (TIKU_I2C_ERR_NACK).
 */
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf,       uint16_t rx_len);

#endif /* TIKU_AMBIQ_I2C_ARCH_H_ */
