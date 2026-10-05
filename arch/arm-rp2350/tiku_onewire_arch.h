/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - RP2350 1-Wire driver interface.
 *
 * The part has no 1-Wire peripheral: the driver toggles one board-selected
 * GPIO with the slot timings DS18B20-family parts need, timed on the 1 us
 * TIMER0 counter so they hold at any clk_sys.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_ONEWIRE_ARCH_H_
#define TIKU_RP2350_ONEWIRE_ARCH_H_

#include <interfaces/onewire/tiku_onewire.h>

/**
 * @brief Configure the 1-Wire GPIO pin and release the bus.
 *
 * Puts TIKU_BOARD_OW_PIN on SIO with its input buffer on and no internal
 * pulls; an external pull-up holds the bus high.
 *
 * @return TIKU_OW_OK.
 */
int     tiku_onewire_arch_init(void);

/**
 * @brief Release the 1-Wire pin and disable its pad's output and input.
 */
void    tiku_onewire_arch_close(void);

/**
 * @brief Issue a 1-Wire reset pulse and detect device presence.
 *
 * Drives the bus low for 480 us, releases it and samples it 70 us later for
 * a device's presence pulse.
 *
 * @note Masks IRQs for about 960 us and enables them on return.
 * @return TIKU_OW_OK if a device answered, TIKU_OW_ERR_NO_DEVICE if the bus
 *         stayed high.
 */
int     tiku_onewire_arch_reset(void);

/**
 * @brief Write a single bit onto the 1-Wire bus.
 *
 * Drives a 70 us write-1 or write-0 slot, timed by busy-waits on the 1 us
 * TIMER0 counter.
 *
 * @note Masks IRQs for the slot and enables them on return.
 * @param bit  Value to write (0 or non-zero).
 */
void    tiku_onewire_arch_write_bit(uint8_t bit);

/**
 * @brief Sample a single bit from the 1-Wire bus.
 *
 * Drives the bus low for 6 us, releases it and samples it 15 us after the
 * slot starts.
 *
 * @note Masks IRQs for the 70 us slot and enables them on return.
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
