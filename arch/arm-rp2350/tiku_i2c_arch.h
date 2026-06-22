/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - RP2350 I2C driver interface
 *
 * Drives the DW_apb_i2c controller (RP2350 datasheet §12.3, plus
 * the DesignWare IP databook). Master mode, 7-bit addressing,
 * supports standard (100 kHz) and fast (400 kHz) modes. Speed is
 * picked from the tiku_i2c_config_t passed to init; SCL high/low
 * counts are recomputed from clk_peri so the same code is correct
 * at every supported clk_sys frequency. SDA/SCL pins are
 * board-defined; the driver verifies the function-select mapping
 * against the per-pin tables in tiku_rp2350_regs.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_I2C_ARCH_H_
#define TIKU_RP2350_I2C_ARCH_H_

#include <interfaces/bus/tiku_i2c_bus.h>

/**
 * @brief Initialize the DW_apb_i2c controller with the given config.
 *
 * Takes the I2C block out of reset, configures SCL high/low counts from
 * clk_peri for the requested speed, and enables the controller.  Must be
 * called before any transfer function.
 *
 * @param config  Bus configuration (speed, pin assignments).
 * @return 0 on success, negative errno-style code on failure.
 */
int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);

/**
 * @brief Disable the I2C controller and release its pins.
 */
void tiku_i2c_arch_close(void);

/**
 * @brief Perform a master write transaction.
 *
 * Sends a START, the 7-bit address with W bit, @p len data bytes from
 * @p buf, then a STOP.  Blocks until the TX FIFO drains or an abort
 * condition is detected.
 *
 * @param addr  7-bit target address (unshifted).
 * @param buf   Byte array to transmit.
 * @param len   Number of bytes to send.
 * @return 0 on success, negative errno-style code on NACK or timeout.
 */
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Perform a master read transaction.
 *
 * Sends a START, the 7-bit address with R bit, reads @p len bytes into
 * @p buf, then issues a STOP with a final NACK.
 *
 * @param addr  7-bit target address (unshifted).
 * @param buf   Destination buffer for received bytes.
 * @param len   Number of bytes to read.
 * @return 0 on success, negative errno-style code on NACK or timeout.
 */
int  tiku_i2c_arch_read (uint8_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific address probe (bus-scan presence check).
 *
 * Reports whether a device acknowledges @p addr.  The DW_apb_i2c cannot do a
 * zero-byte transaction, so the backend probes with a single 1-byte read.
 * The `i2c scan` command uses this instead of a zero-length write (which the
 * bus layer rejects).
 *
 * @param addr  7-bit slave address (unshifted).
 * @return 0 (TIKU_I2C_OK) if acknowledged, negative errno-style code if not.
 */
int  tiku_i2c_arch_probe(uint8_t addr);

/**
 * @brief Perform a combined write-then-read transaction (repeated START).
 *
 * Writes @p tx_len bytes, issues a repeated START without releasing the
 * bus, then reads @p rx_len bytes.  Used for register-addressed sensors
 * (write register address, read back value).
 *
 * @param addr    7-bit target address (unshifted).
 * @param tx_buf  Bytes to transmit in the write phase.
 * @param tx_len  Number of bytes to write.
 * @param rx_buf  Destination buffer for the read phase.
 * @param rx_len  Number of bytes to read.
 * @return 0 on success, negative errno-style code on failure.
 */
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf,       uint16_t rx_len);

#endif /* TIKU_RP2350_I2C_ARCH_H_ */
