/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_i2c_arch.h - C5 I2C0 blocking master on board-selected GPIOs.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_I2C_ARCH_H_
#define TIKU_ESP32C5_I2C_ARCH_H_
#include <interfaces/bus/tiku_i2c_bus.h>

/** @brief Claim SDA/SCL and configure 100/400 kHz from the 48 MHz crystal.
 * @return OK, PARAM, BUSY for owned pins/controller, or TIMEOUT on sync
 * failure.
 * @note Scheduler context only, serialized with other bus calls; not
 * ISR/worker. Requires external pull-ups. Invalid configuration leaves an open
 * bus intact.
 */
int tiku_i2c_arch_init(const tiku_i2c_config_t *config);
/** @brief Stop the controller, restore pin routing and release both claims.
 * @note Scheduler context only; requires no active transfer.
 */
void tiku_i2c_arch_close(void);
/** @brief Write 1..65535 bytes to a seven-bit address, with one START and STOP.
 * @note Scheduler context only. An error can follow a partially completed
 * write.
 */
int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);
/** @brief Read 1..65535 bytes; NACK the final byte, then send STOP.
 * @note Scheduler context only. An error can leave a received prefix in buf.
 */
int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len);
/** @brief Send START, address+W and STOP without a data byte.
 * @return OK, NACK, BUSY (including arbitration loss), TIMEOUT or PARAM.
 * @note Scheduler context only; zero-length writes use this entry point.
 */
int tiku_i2c_arch_probe(uint8_t addr);
/** @brief Write then read without an intervening STOP; both lengths must be
 * nonzero.
 * @note Scheduler context only. Each FIFO batch has a bounded wait; no retry is
 *       performed after an error. Caller buffers may contain a completed
 * prefix. A failed controller reset requires close/reinit before another
 * transfer.
 */
int tiku_i2c_arch_write_read(uint8_t addr, const uint8_t *tx, uint16_t tx_len,
                             uint8_t *rx, uint16_t rx_len);
#endif
