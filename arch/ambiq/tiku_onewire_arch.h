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

int     tiku_onewire_arch_init(void);
void    tiku_onewire_arch_close(void);
int     tiku_onewire_arch_reset(void);
void    tiku_onewire_arch_write_bit(uint8_t bit);
uint8_t tiku_onewire_arch_read_bit(void);
void    tiku_onewire_arch_write_byte(uint8_t byte);
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_AMBIQ_ONEWIRE_ARCH_H_ */
