/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - Apollo 510 I2C driver interface
 *
 * Stub at this milestone (returns not-supported); a real am_hal_iom
 * (I/O Master) backend lands with the peripheral pass.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_I2C_ARCH_H_
#define TIKU_AMBIQ_I2C_ARCH_H_

#include <interfaces/bus/tiku_i2c_bus.h>

/**
 * @brief Initialize the I2C peripheral with the given configuration.
 *
 * Stub — returns a not-supported error code. A real am_hal_iom
 * (I/O Master) backend will replace this in the peripheral pass.
 *
 * @param config  Pointer to the I2C configuration structure.
 * @return 0 on success, negative error code on failure.
 */
int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);

/**
 * @brief Release the I2C peripheral and power it down.
 *
 * Stub — no-op at this milestone.
 */
void tiku_i2c_arch_close(void);

/**
 * @brief Perform a blocking I2C write transaction.
 *
 * Stub — returns a not-supported error code.
 *
 * @param addr  7-bit I2C target address (unshifted).
 * @param buf   Data buffer to transmit.
 * @param len   Number of bytes to write.
 * @return 0 on success, negative error code on failure.
 */
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Perform a blocking I2C read transaction.
 *
 * Stub — returns a not-supported error code.
 *
 * @param addr  7-bit I2C target address (unshifted).
 * @param buf   Destination buffer for received bytes.
 * @param len   Number of bytes to read.
 * @return 0 on success, negative error code on failure.
 */
int  tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific address probe (bus-scan presence check).
 *
 * Reports whether a device acknowledges @p addr without transferring data.
 * The `i2c scan` command uses this instead of a zero-length write (which the
 * bus layer rejects).  Stub on Apollo until the am_hal_iom backend (#31).
 *
 * @param addr  7-bit slave address (unshifted).
 * @return 0 (TIKU_I2C_OK) if acknowledged, negative error code if not.
 */
int  tiku_i2c_arch_probe(uint8_t addr);

/**
 * @brief Perform a combined I2C write-then-read transaction.
 *
 * Issues a write followed immediately by a repeated-START and read
 * to the same device address. Commonly used for register reads on
 * I2C sensors and EEPROMs.
 *
 * Stub — returns a not-supported error code.
 *
 * @param addr    7-bit I2C target address (unshifted).
 * @param tx_buf  Data buffer to transmit in the write phase.
 * @param tx_len  Number of bytes to write.
 * @param rx_buf  Destination buffer for the read phase.
 * @param rx_len  Number of bytes to read.
 * @return 0 on success, negative error code on failure.
 */
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf,       uint16_t rx_len);

#endif /* TIKU_AMBIQ_I2C_ARCH_H_ */
