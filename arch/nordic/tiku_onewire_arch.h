/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - nRF54L 1-Wire arch header (stub port)
 *
 * The 1-Wire backend is a stub on this port (see tiku_onewire_arch.c); a real
 * bit-banged driver is a later phase.  These prototypes mirror the RP2350 arch
 * header so the interface layer (interfaces/onewire/tiku_onewire.c) and the
 * 1-Wire HAL routing resolve without implicit declarations on Nordic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_ONEWIRE_ARCH_H_
#define TIKU_NORDIC_ONEWIRE_ARCH_H_

#include <stdint.h>
#include <interfaces/onewire/tiku_onewire.h>

/**
 * @brief Configure the 1-Wire pin and bring up the bus (stub).
 *
 * Not implemented on this port: a bit-banged 1-Wire master needs a
 * microsecond-class delay source, which is a later phase.  Reports
 * failure instead of pretending the bus came up, so callers fall
 * through to their no-device path.
 *
 * @return -1 always (never TIKU_OW_OK).
 */
int     tiku_onewire_arch_init(void);

/**
 * @brief Release the 1-Wire pin (stub -- no-op).
 */
void    tiku_onewire_arch_close(void);

/**
 * @brief Issue a reset pulse and sample for a presence pulse (stub).
 *
 * @return -1 always, i.e. TIKU_OW_ERR_NO_DEVICE: without the bit-bang
 *         timing backend no presence detection is possible.
 */
int     tiku_onewire_arch_reset(void);

/**
 * @brief Write a single bit onto the bus (stub -- no-op).
 *
 * @param bit  Bit value to write (0 or 1); ignored.
 */
void    tiku_onewire_arch_write_bit(uint8_t bit);

/**
 * @brief Sample a single bit from the bus (stub).
 *
 * @return 1 always -- the level an idle, externally pulled-up 1-Wire
 *         line reads back, rather than fabricated device data.
 */
uint8_t tiku_onewire_arch_read_bit(void);

/**
 * @brief Write a byte LSB-first onto the bus (stub -- no-op).
 *
 * @param byte  Byte value to transmit; ignored.
 */
void    tiku_onewire_arch_write_byte(uint8_t byte);

/**
 * @brief Read a byte LSB-first from the bus (stub).
 *
 * @return 0xFF always -- every bit read back from an idle, pulled-high
 *         bus (see tiku_onewire_arch_read_bit()).
 */
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_NORDIC_ONEWIRE_ARCH_H_ */
