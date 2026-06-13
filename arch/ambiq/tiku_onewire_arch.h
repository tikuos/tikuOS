/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - Apollo 510 1-Wire driver interface
 *
 * Stub at this milestone (returns not-supported). A GPIO bit-bang
 * implementation (mirroring RP2350) lands with the peripheral pass once
 * the htimer microsecond source is wired.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_ONEWIRE_ARCH_H_
#define TIKU_AMBIQ_ONEWIRE_ARCH_H_

#include <interfaces/onewire/tiku_onewire.h>

/**
 * @brief Initialize the 1-Wire bus driver.
 *
 * Stub — returns a not-supported error code. A GPIO bit-bang
 * backend (mirroring RP2350) will replace this once the htimer
 * microsecond source is wired in the peripheral pass.
 *
 * @return 0 on success, negative error code on failure.
 */
int     tiku_onewire_arch_init(void);

/**
 * @brief Release the 1-Wire bus driver.
 *
 * Stub — no-op at this milestone.
 */
void    tiku_onewire_arch_close(void);

/**
 * @brief Issue a 1-Wire reset pulse and detect device presence.
 *
 * Stub — returns a not-supported error code.
 *
 * @return 1 if at least one device responded with a presence pulse,
 *         0 if no device responded, negative error code on failure.
 */
int     tiku_onewire_arch_reset(void);

/**
 * @brief Write a single bit onto the 1-Wire bus.
 *
 * Stub — no-op at this milestone.
 *
 * @param bit  Bit value to write (0 or 1).
 */
void    tiku_onewire_arch_write_bit(uint8_t bit);

/**
 * @brief Read a single bit from the 1-Wire bus.
 *
 * Stub — returns 0 at this milestone.
 *
 * @return Bit value read (0 or 1).
 */
uint8_t tiku_onewire_arch_read_bit(void);

/**
 * @brief Write one byte (8 bits, LSB first) onto the 1-Wire bus.
 *
 * Stub — no-op at this milestone.
 *
 * @param byte  Byte value to transmit.
 */
void    tiku_onewire_arch_write_byte(uint8_t byte);

/**
 * @brief Read one byte (8 bits, LSB first) from the 1-Wire bus.
 *
 * Stub — returns 0 at this milestone.
 *
 * @return Byte value received.
 */
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_AMBIQ_ONEWIRE_ARCH_H_ */
