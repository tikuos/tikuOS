/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - 1-Wire bus driver for MSP430 (architecture layer)
 *
 * Declares the architecture-specific 1-Wire functions implemented by
 * tiku_onewire_arch.c using GPIO bit-banging. These are called by the
 * platform-independent layer (interfaces/onewire/tiku_onewire.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ONEWIRE_ARCH_H_
#define TIKU_ONEWIRE_ARCH_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <interfaces/onewire/tiku_onewire.h>

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

int tiku_onewire_arch_init(void);
void tiku_onewire_arch_close(void);
int tiku_onewire_arch_reset(void);
void tiku_onewire_arch_write_bit(uint8_t bit);
uint8_t tiku_onewire_arch_read_bit(void);
void tiku_onewire_arch_write_byte(uint8_t byte);
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_ONEWIRE_ARCH_H_ */
