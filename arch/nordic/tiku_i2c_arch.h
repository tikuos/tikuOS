/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.h - nRF54L I2C (TWIM) arch header.
 *
 * Declares the blocking TWIM (EasyDMA) I2C master that the I2C bus interface
 * calls through hal/tiku_i2c_hal.h.
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
 * Parks SDA/SCL as open-drain pull-up pins, routes them to the TWIM via PSEL,
 * selects 100 kHz (Standard) or 400 kHz (Fast) and enables the peripheral.
 * Pins default to P1.11 (SDA) / P1.12 (SCL), overridable from the board header.
 *
 * @note The instance is TWIM22 (SERIAL22): TWIM20 aliases the console UARTE20,
 *       and a SERIALn base can only be UARTE, SPIM or TWIM at a time.  External
 *       4.7 kohm pull-ups are recommended -- the internal pull-up is weak.
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
 * One EasyDMA transaction closed by the LASTTX->STOP short.
 *
 * @note Non-RAM writes of at most 256 bytes use RAM staging. Larger
 *       non-RAM writes return TIKU_I2C_ERR_PARAM; RAM buffers use DMA directly.
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
 * One EasyDMA transaction closed by the LASTRX->STOP short.
 *
 * @note @p buf must be in RAM.
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
 * Writes one byte, because the TWIM does not clock an address for a
 * zero-length transfer.  Only the address ACK decides the result: a data NACK
 * after an acknowledged address returns TIKU_I2C_OK.
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
