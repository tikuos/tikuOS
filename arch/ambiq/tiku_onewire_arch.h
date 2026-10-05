/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - Ambiq 1-Wire driver interface.
 *
 * This port has no 1-Wire driver: init and reset return -1, the reads return
 * an idle bus (bit 1, byte 0xFF), and the writes and close do nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_ONEWIRE_ARCH_H_
#define TIKU_AMBIQ_ONEWIRE_ARCH_H_

#include <interfaces/onewire/tiku_onewire.h>

/**
 * @brief Initialize the 1-Wire bus; fails on every call on this port.
 *
 * @return -1
 */
int     tiku_onewire_arch_init(void);

/** @brief Release the 1-Wire bus driver; does nothing on this port. */
void    tiku_onewire_arch_close(void);

/**
 * @brief Issue a 1-Wire reset pulse; fails on every call on this port.
 *
 * @return TIKU_OW_ERR_NO_DEVICE (-1)
 */
int     tiku_onewire_arch_reset(void);

/**
 * @brief Write a single bit onto the 1-Wire bus; does nothing on this port.
 *
 * @param bit  Bit value to write (0 or 1).
 */
void    tiku_onewire_arch_write_bit(uint8_t bit);

/**
 * @brief Read a single bit from the 1-Wire bus; this port reads no pin.
 *
 * @return 1, the level of an idle bus
 */
uint8_t tiku_onewire_arch_read_bit(void);

/**
 * @brief Write one byte (8 bits, LSB first) onto the 1-Wire bus; does nothing
 *        on this port.
 *
 * @param byte  Byte value to transmit.
 */
void    tiku_onewire_arch_write_byte(uint8_t byte);

/**
 * @brief Read one byte (8 bits, LSB first) from the 1-Wire bus; this port
 *        reads no pin.
 *
 * @return 0xFF, the bits of an idle bus
 */
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_AMBIQ_ONEWIRE_ARCH_H_ */
