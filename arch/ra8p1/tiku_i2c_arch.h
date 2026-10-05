/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - RA8P1 I2C master.
 *
 * A polled 7-bit master on IIC channel 1 (SCL1 P512, SDA1 P511) at about
 * 370 kHz; the speed in the config is ignored.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_I2C_ARCH_H_
#define TIKU_RA8P1_I2C_ARCH_H_

#include <interfaces/bus/tiku_i2c_bus.h>

/**
 * @brief Power IIC1, clock any stuck slave off the bus and configure it.
 *
 * A second call returns TIKU_I2C_OK without touching the bus.
 *
 * @param config  Ignored; the rate is fixed at about 370 kHz
 * @return TIKU_I2C_OK
 */
int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);

/** @brief Disable the IIC unit; its module clock stays on. */
void tiku_i2c_arch_close(void);

/**
 * @brief Write bytes to a device.
 *
 * @param addr  7-bit address
 * @param buf   Bytes to send
 * @param len   Length
 * @return TIKU_I2C_OK, or TIKU_I2C_ERR_PARAM, _BUSY, _TIMEOUT or _NACK
 */
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Read bytes from a device.
 *
 * @param addr  7-bit address
 * @param buf   Receives the bytes
 * @param len   Length, at least 1
 * @return TIKU_I2C_OK, or TIKU_I2C_ERR_PARAM, _BUSY, _TIMEOUT or _NACK
 */
int  tiku_i2c_arch_read (uint8_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief Address a device and stop, transferring no data.
 *
 * @param addr  7-bit address
 * @return TIKU_I2C_OK when a device acknowledges, TIKU_I2C_ERR_NACK when
 *         none does, or another error
 */
int  tiku_i2c_arch_probe(uint8_t addr);

/**
 * @brief Write then read without releasing the bus: a repeated start
 *        separates the two.
 *
 * @param addr    7-bit address
 * @param tx_buf  Bytes to send
 * @param tx_len  Send length
 * @param rx_buf  Receives the reply
 * @param rx_len  Reply length, at least 1
 * @return TIKU_I2C_OK, or TIKU_I2C_ERR_PARAM, _BUSY, _TIMEOUT or _NACK
 */
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf,       uint16_t rx_len);

#endif /* TIKU_RA8P1_I2C_ARCH_H_ */
