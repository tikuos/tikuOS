/*
 * Tiku Operating System v0.05
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

int     tiku_onewire_arch_init(void);
void    tiku_onewire_arch_close(void);
int     tiku_onewire_arch_reset(void);
void    tiku_onewire_arch_write_bit(uint8_t bit);
uint8_t tiku_onewire_arch_read_bit(void);
void    tiku_onewire_arch_write_byte(uint8_t byte);
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_NORDIC_ONEWIRE_ARCH_H_ */
