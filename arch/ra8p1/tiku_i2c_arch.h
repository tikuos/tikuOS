/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - RA8P1 I2C contract.
 *
 * No backend on this port yet: the calls exist so the kernel links, and each
 * reports failure rather than pretending a transfer happened.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_I2C_ARCH_H_
#define TIKU_RA8P1_I2C_ARCH_H_

#include <interfaces/bus/tiku_i2c_bus.h>

/** @brief Configure a bus. @param config  Requested settings @return Error */
int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);

/** @brief Release the bus. */
void tiku_i2c_arch_close(void);

/**
 * @brief Write bytes to a device.
 *
 * @param addr  7-bit address
 * @param buf   Bytes to send
 * @param len   Length
 * @return TIKU_I2C_OK, or an error
 */
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Read bytes from a device.
 *
 * @param addr  7-bit address
 * @param buf   Receives the bytes
 * @param len   Length
 * @return TIKU_I2C_OK, or an error
 */
int  tiku_i2c_arch_read (uint8_t addr, uint8_t *buf, uint16_t len);

/** @brief Test for a device. @param addr  7-bit address @return Error */
int  tiku_i2c_arch_probe(uint8_t addr);

/**
 * @brief Write then read without releasing the bus.
 *
 * @param addr    7-bit address
 * @param tx_buf  Bytes to send
 * @param tx_len  Send length
 * @param rx_buf  Receives the reply
 * @param rx_len  Reply length
 * @return TIKU_I2C_OK, or an error
 */
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf,       uint16_t rx_len);

#endif /* TIKU_RA8P1_I2C_ARCH_H_ */
