/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - STM32N6 I2C stub.
 *
 * This port has no I2C driver: every call returns TIKU_I2C_ERR_PARAM and
 * moves no data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_I2C_ARCH_H_
#define TIKU_STM32N6_I2C_ARCH_H_

#include <interfaces/bus/tiku_i2c_bus.h>

/** @brief Ignores @p config and returns TIKU_I2C_ERR_PARAM. */
int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);

/** @brief Does nothing. */
void tiku_i2c_arch_close(void);

/** @brief Sends nothing and returns TIKU_I2C_ERR_PARAM. */
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/** @brief Leaves @p buf untouched and returns TIKU_I2C_ERR_PARAM. */
int  tiku_i2c_arch_read (uint8_t addr, uint8_t *buf, uint16_t len);

/** @brief Probes nothing and returns TIKU_I2C_ERR_PARAM. */
int  tiku_i2c_arch_probe(uint8_t addr);

/**
 * @brief Sends nothing, leaves @p rx_buf untouched and returns
 *        TIKU_I2C_ERR_PARAM.
 */
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf,       uint16_t rx_len);

#endif /* TIKU_STM32N6_I2C_ARCH_H_ */
