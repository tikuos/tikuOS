/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - Ambiq 1-Wire driver, a stub.
 *
 * This port has no 1-Wire driver: init and reset return -1, the reads return
 * an idle bus (bit 1, byte 0xFF), and the writes and close do nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_onewire_arch.h"

/**
 * @brief Initialize the 1-Wire bus (stub)
 *
 * @return -1 always
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
 * @return -1 (TIKU_OW_ERR_NO_DEVICE) always
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
 * @return 1 always, the level of an idle bus
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
 * @return 0xFF always, the bits of an idle bus
 */
uint8_t tiku_onewire_arch_read_byte(void) {
    return 0xFF;
}
