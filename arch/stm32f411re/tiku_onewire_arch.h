/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_onewire_arch.h - STM32F411RE 1-Wire driver interface
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_ONEWIRE_ARCH_H_
#define TIKU_STM32F411_ONEWIRE_ARCH_H_

#include <interfaces/onewire/tiku_onewire.h>

int     tiku_onewire_arch_init(void);
void    tiku_onewire_arch_close(void);
int     tiku_onewire_arch_reset(void);
void    tiku_onewire_arch_write_bit(uint8_t bit);
uint8_t tiku_onewire_arch_read_bit(void);
void    tiku_onewire_arch_write_byte(uint8_t byte);
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_STM32F411_ONEWIRE_ARCH_H_ */
