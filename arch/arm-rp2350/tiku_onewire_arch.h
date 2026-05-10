/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - RP2350 1-Wire stub
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
