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

int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);
void tiku_i2c_arch_close(void);
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);
int  tiku_i2c_arch_read (uint8_t addr, uint8_t *buf, uint16_t len);
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf,       uint16_t rx_len);

#endif /* TIKU_RP2350_I2C_ARCH_H_ */
