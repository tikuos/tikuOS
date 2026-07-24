/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - RP2350 1-Wire driver interface
 *
 * GPIO bit-bang implementation -- the RP2350 has no dedicated
 * 1-Wire peripheral, so the driver toggles a single GPIO with
 * tightly-timed delays. The pin is selected at compile time via
 * TIKU_BOARD_OW_PIN in the board header. Reset, read-bit,
 * write-bit, and byte-level helpers cover the slot timings
 * required by DS18B20 family parts (and any Maxim 1-Wire
 * sensor with the same protocol).
 *
 * The bit-bang loop spins on the 1 us TIMER0 tick, so the timing
 * is invariant under clk_sys frequency changes -- the same
 * firmware bit-bangs at the right rate at 12 MHz and 150 MHz.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_ONEWIRE_ARCH_H_
#define TIKU_RP2350_ONEWIRE_ARCH_H_

#include <interfaces/onewire/tiku_onewire.h>

/**
 * @brief Configure the 1-Wire GPIO pin and enable the bit-bang driver.
 *
 * Sets TIKU_BOARD_OW_PIN as an open-drain output (pull-up via the pad
 * register) and arms the TIMER0 1 us tick used for slot timing.
 *
 * @return 0 on success, negative code if the pin or timer is unavailable.
 */
int     tiku_onewire_arch_init(void);

/**
 * @brief Release the 1-Wire GPIO pin (return to high-impedance input).
 */
void    tiku_onewire_arch_close(void);

/**
 * @brief Issue a 1-Wire reset pulse and detect device presence.
 *
 * Pulls the bus low for 480 us, releases it, then samples the presence
 * pulse within the 60–240 us window required by the 1-Wire spec.
 *
 * @return 1 if at least one device responded, 0 if the bus stayed high
 *         (no devices), or a negative code on timing failure.
 */
int     tiku_onewire_arch_reset(void);

/**
 * @brief Write a single bit onto the 1-Wire bus.
 *
 * Drives a write-1 or write-0 slot according to the 1-Wire spec.
 * Timing is enforced by 1 us TIMER0 busy-waits.
 *
 * @param bit  Value to write (0 or non-zero).
 */
void    tiku_onewire_arch_write_bit(uint8_t bit);

/**
 * @brief Sample a single bit from the 1-Wire bus.
 *
 * Drives the bus low for the required initiation window, releases it,
 * then samples within the 15 us read window.
 *
 * @return Sampled bit value (0 or 1).
 */
uint8_t tiku_onewire_arch_read_bit(void);

/**
 * @brief Write a byte LSB-first onto the 1-Wire bus.
 *
 * @param byte  Byte to transmit.
 */
void    tiku_onewire_arch_write_byte(uint8_t byte);

/**
 * @brief Read a byte LSB-first from the 1-Wire bus.
 *
 * @return Received byte.
 */
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_RP2350_ONEWIRE_ARCH_H_ */
