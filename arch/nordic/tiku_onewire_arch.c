/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - nRF54L 1-Wire bit-bang (stub — not yet wired)
 *
 * Honest placeholder for the GPIO bit-bang 1-Wire driver. init/reset
 * report failure and bus reads return the idle-high pattern (bit = 1,
 * byte = 0xFF) rather than fake device data. A real bit-bang backend
 * (which needs a microsecond time source) is a later phase.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_onewire_arch.h>

/**
 * @brief Configure the 1-Wire pin and enable the driver (stub).
 *
 * @return -1 always (not supported yet).
 */
int tiku_onewire_arch_init(void)
{
    return -1;
}

/**
 * @brief Release the 1-Wire pin (stub — no-op).
 */
void tiku_onewire_arch_close(void)
{
}

/**
 * @brief Issue a reset pulse and detect presence (stub).
 *
 * @return -1 always (no presence detection possible; not supported yet).
 */
int tiku_onewire_arch_reset(void)
{
    return -1;
}

/**
 * @brief Write a single bit onto the bus (stub — no-op).
 *
 * @param bit  Bit value to write (ignored).
 */
void tiku_onewire_arch_write_bit(uint8_t bit)
{
    (void)bit;
}

/**
 * @brief Sample a single bit from the bus (stub).
 *
 * @return 1 always (an idle, pulled-high 1-Wire bus reads as 1).
 */
uint8_t tiku_onewire_arch_read_bit(void)
{
    return 1;
}

/**
 * @brief Write a byte LSB-first onto the bus (stub — no-op).
 *
 * @param byte  Byte value to transmit (ignored).
 */
void tiku_onewire_arch_write_byte(uint8_t byte)
{
    (void)byte;
}

/**
 * @brief Read a byte LSB-first from the bus (stub).
 *
 * @return 0xFF always (all bits read back from an idle, pulled-high bus).
 */
uint8_t tiku_onewire_arch_read_byte(void)
{
    return 0xFF;
}
