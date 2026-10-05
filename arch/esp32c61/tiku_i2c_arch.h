/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - ESP32-C61 I2C master contract.
 *
 * This port has no I2C driver: every call that returns a code returns
 * TIKU_I2C_ERR_PARAM, and no call touches a buffer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_I2C_ARCH_H_
#define TIKU_ESP32C61_I2C_ARCH_H_

#include <interfaces/bus/tiku_i2c_bus.h>

/**
 * @brief Configures nothing.
 * @param config  Ignored
 * @return TIKU_I2C_ERR_PARAM
 */
int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);

/** @brief Does nothing. */
void tiku_i2c_arch_close(void);

/** @brief Sends nothing. @return TIKU_I2C_ERR_PARAM */
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/** @brief Reads nothing into @p buf. @return TIKU_I2C_ERR_PARAM */
int  tiku_i2c_arch_read (uint8_t addr, uint8_t *buf, uint16_t len);

/** @brief Probes nothing. @return TIKU_I2C_ERR_PARAM */
int  tiku_i2c_arch_probe(uint8_t addr);

/** @brief Transfers nothing. @return TIKU_I2C_ERR_PARAM */
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf,       uint16_t rx_len);

#endif /* TIKU_ESP32C61_I2C_ARCH_H_ */
