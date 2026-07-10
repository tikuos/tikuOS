/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - nRF54L I2C (TWIM) arch header (stub port)
 *
 * The I2C backend is a stub on this port (see tiku_i2c_arch.c); a real TWIM
 * driver is a later phase.  These prototypes mirror the RP2350 arch header so
 * the interface layer (interfaces/bus/tiku_i2c_bus.c) and the I2C HAL routing
 * (hal/tiku_i2c_hal.h) resolve without implicit declarations on Nordic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_I2C_ARCH_H_
#define TIKU_NORDIC_I2C_ARCH_H_

#include <stdint.h>
#include <interfaces/bus/tiku_i2c_bus.h>

int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);
void tiku_i2c_arch_close(void);
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);
int  tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len);
int  tiku_i2c_arch_probe(uint8_t addr);
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf, uint16_t rx_len);

#endif /* TIKU_NORDIC_I2C_ARCH_H_ */
