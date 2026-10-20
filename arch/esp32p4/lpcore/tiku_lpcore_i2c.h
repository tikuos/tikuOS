/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpcore_i2c.h - an I2C controller bit-banged on two LP IOs (P4 LP core
 * payload), on the payload's line hooks, so a host model can run it against
 * a simulated device.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LPCORE_I2C_H_
#define TIKU_LPCORE_I2C_H_

#include <stdint.h>

/** @brief Release LP IO @p n (open drain) for 1, pull it low for 0 (the
 *         payload's). */
void tiku_lpcore_io_set(uint32_t n, uint32_t high);
/** @brief The pad level of LP IO @p n, 0 or 1 (the payload's). */
uint32_t tiku_lpcore_io_get(uint32_t n);
/** @brief Wait one half bit of the bus clock (the payload's). */
void tiku_lpcore_i2c_delay(void);

/**
 * @brief A START, the address byte for a write to @p addr, then a STOP.
 * @return 1 acknowledged, 0 not, -1 when SCL never rose
 */
int tiku_lpcore_i2c_probe(uint32_t sda, uint32_t scl, uint32_t addr);

/**
 * @brief Read register @p reg of the device at @p addr: the register byte
 *        written, a repeated START, then @p bytes bytes read (1, or 2 taken
 *        as a big-endian word), the last one not acknowledged.
 * @return The value, or -1 on a NACK or when SCL never rose
 */
int32_t tiku_lpcore_i2c_read(uint32_t sda, uint32_t scl, uint32_t addr,
                             uint32_t reg, uint32_t bytes);

#endif /* TIKU_LPCORE_I2C_H_ */
