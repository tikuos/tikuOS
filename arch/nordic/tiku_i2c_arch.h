/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - nRF54L I2C (TWIM) arch header
 *
 * Blocking TWIM (I2C master) backend for the nRF54L (see tiku_i2c_arch.c).
 * These prototypes mirror the RP2350 arch header so the interface layer
 * (interfaces/bus/tiku_i2c_bus.c) and the I2C HAL routing (hal/tiku_i2c_hal.h)
 * resolve without implicit declarations on Nordic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_I2C_ARCH_H_
#define TIKU_NORDIC_I2C_ARCH_H_

#include <stdint.h>
#include <interfaces/bus/tiku_i2c_bus.h>

/**
 * @brief Architecture-specific I2C initialization.
 *
 * Parks SDA/SCL as open-drain pull-up pins, routes them to the TWIM via
 * PSEL, selects 100 kHz (Standard) or 400 kHz (Fast) and enables the
 * peripheral.  The instance is TWIM22 (SERIAL22): TWIM20 aliases the
 * console UARTE20, and a SERIALn base can only be UARTE, SPIM or TWIM at
 * a time.  Pins default to P1.11 (SDA) / P1.12 (SCL) and are overridable
 * from the board header.  External 4.7 kohm pull-ups are recommended --
 * the internal pull-up is weak.
 *
 * @param config  Pointer to I2C configuration (speed)
 * @return TIKU_I2C_OK on success, TIKU_I2C_ERR_PARAM if @p config is NULL
 */
int  tiku_i2c_arch_init(const tiku_i2c_config_t *config);

/**
 * @brief Architecture-specific I2C shutdown.
 *
 * Clears ENABLE, disconnects the PSEL routing so SDA/SCL revert to plain
 * GPIO, and marks the driver uninitialised.
 */
void tiku_i2c_arch_close(void);

/**
 * @brief Architecture-specific I2C write (START, addr+W, data, STOP).
 *
 * One EasyDMA transaction closed by the LASTTX->STOP short.  @p buf must
 * be in RAM -- TWIM EasyDMA cannot fetch from the RRAM code region.
 *
 * @param addr  7-bit slave address (unshifted)
 * @param buf   Data to transmit
 * @param len   Number of bytes; 0 returns TIKU_I2C_OK immediately
 * @return TIKU_I2C_OK on success, TIKU_I2C_ERR_PARAM if uninitialised or
 *         @p buf is NULL with len > 0, TIKU_I2C_ERR_NACK on an address or
 *         data NACK, TIKU_I2C_ERR_TIMEOUT if the bus never reaches STOP
 */
int  tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific I2C read (START, addr+R, data, NACK, STOP).
 *
 * One EasyDMA transaction closed by the LASTRX->STOP short; @p buf must
 * be in RAM.
 *
 * @param addr  7-bit slave address (unshifted)
 * @param buf   Buffer for received data
 * @param len   Number of bytes; 0 returns TIKU_I2C_OK immediately
 * @return TIKU_I2C_OK on success, TIKU_I2C_ERR_PARAM if uninitialised or
 *         @p buf is NULL, TIKU_I2C_ERR_NACK on slave NACK,
 *         TIKU_I2C_ERR_TIMEOUT if the bus never reaches STOP
 */
int  tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief Architecture-specific address probe (bus-scan presence check).
 *
 * The nRF TWIM will not clock an address for a zero-length transfer, so
 * the probe is a real 1-byte write and presence is decided solely by the
 * address ACK.  A data NACK (DNACK) still proves the device answered and
 * is deliberately not reported as absence.
 *
 * @param addr  7-bit slave address (unshifted)
 * @return TIKU_I2C_OK if the address was acknowledged, TIKU_I2C_ERR_NACK
 *         if not (ANACK), TIKU_I2C_ERR_PARAM if uninitialised,
 *         TIKU_I2C_ERR_TIMEOUT on a wedged bus
 */
int  tiku_i2c_arch_probe(uint8_t addr);

/**
 * @brief Architecture-specific combined write-then-read.
 *
 * Runs START, addr+W, tx, repeated START, addr+R, rx, STOP as a single
 * hardware transaction (LASTTX->STARTRX + LASTRX->STOP shorts).  A
 * zero-length side degrades to a plain write or plain read.
 *
 * @param addr    7-bit slave address (unshifted)
 * @param tx_buf  Data to transmit (e.g. a register address)
 * @param tx_len  Transmit length; may be 0
 * @param rx_buf  Buffer for received data
 * @param rx_len  Receive length; may be 0
 * @return TIKU_I2C_OK on success, TIKU_I2C_ERR_PARAM if uninitialised or
 *         a non-zero length has a NULL buffer, TIKU_I2C_ERR_NACK on slave
 *         NACK, TIKU_I2C_ERR_TIMEOUT on a wedged bus
 */
int  tiku_i2c_arch_write_read(uint8_t addr,
                              const uint8_t *tx_buf, uint16_t tx_len,
                              uint8_t *rx_buf, uint16_t rx_len);

#endif /* TIKU_NORDIC_I2C_ARCH_H_ */
