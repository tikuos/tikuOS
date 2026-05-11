/*
 * Tiku Operating System v0.04
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

int     tiku_onewire_arch_init(void);
void    tiku_onewire_arch_close(void);
int     tiku_onewire_arch_reset(void);
void    tiku_onewire_arch_write_bit(uint8_t bit);
uint8_t tiku_onewire_arch_read_bit(void);
void    tiku_onewire_arch_write_byte(uint8_t byte);
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_RP2350_ONEWIRE_ARCH_H_ */
