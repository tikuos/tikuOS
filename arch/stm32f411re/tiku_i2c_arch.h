/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_i2c_arch.h - STM32F411RE I2C driver interface
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_I2C_ARCH_H_
#define TIKU_STM32F411_I2C_ARCH_H_

#include <interfaces/bus/tiku_i2c_bus.h>

int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);
void tiku_i2c_arch_close(void);
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);
int  tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len);
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf, uint16_t rx_len);

#endif /* TIKU_STM32F411_I2C_ARCH_H_ */
