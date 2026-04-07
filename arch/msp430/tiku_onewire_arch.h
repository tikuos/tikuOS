/*
 * Tiku Operating System v0.02
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
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
