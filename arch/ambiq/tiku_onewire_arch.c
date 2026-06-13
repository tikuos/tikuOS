/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - Apollo 510 1-Wire driver (stub)
 *
 * Not yet supported. A GPIO bit-bang implementation lands with the
 * peripheral pass once the htimer microsecond source is wired.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_onewire_arch.h"

/**
 * @brief Initialize the 1-Wire bus (stub — not yet implemented)
 *
 * A GPIO bit-bang implementation lands with the peripheral pass once
 * the htimer microsecond source is wired. Until then every entry point
 * returns a hard failure so callers can detect the missing backend.
 *
 * @return -1 always (unsupported)
 */
int tiku_onewire_arch_init(void) {
    return -1;
}

/**
 * @brief Release the 1-Wire bus (stub — no-op)
 */
void tiku_onewire_arch_close(void) {
}

/**
 * @brief Issue a 1-Wire reset pulse and check for device presence (stub)
 *
 * @return -1 always (unsupported)
 */
int tiku_onewire_arch_reset(void) {
    return -1;
}

/**
 * @brief Write one bit onto the 1-Wire bus (stub — no-op)
 *
 * @param bit  Bit value to write (0 or 1)
 */
void tiku_onewire_arch_write_bit(uint8_t bit) {
    (void)bit;
}

/**
 * @brief Read one bit from the 1-Wire bus (stub)
 *
 * @return 1 always (bus passive-high / no device present)
 */
uint8_t tiku_onewire_arch_read_bit(void) {
    return 1;
}

/**
 * @brief Write one byte onto the 1-Wire bus, LSB first (stub — no-op)
 *
 * @param byte  Byte value to transmit
 */
void tiku_onewire_arch_write_byte(uint8_t byte) {
    (void)byte;
}

/**
 * @brief Read one byte from the 1-Wire bus, LSB first (stub)
 *
 * @return 0xFF always (all bits passive-high)
 */
uint8_t tiku_onewire_arch_read_byte(void) {
    return 0xFF;
}
