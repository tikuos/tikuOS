/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire.c - Platform-independent 1-Wire bus implementation
 *
 * Delegates to the architecture-specific 1-Wire driver via the
 * HAL routing header.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_onewire.h"
#include "tiku.h"

#ifdef TIKU_BOARD_OW_AVAILABLE  /* Board supports 1-Wire */

#include <hal/tiku_onewire_hal.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_onewire_init(void)
{
    return tiku_onewire_arch_init();
}

void
tiku_onewire_close(void)
{
    tiku_onewire_arch_close();
}

int
tiku_onewire_reset(void)
{
    return tiku_onewire_arch_reset();
}

void
tiku_onewire_write_bit(uint8_t bit)
{
    tiku_onewire_arch_write_bit(bit);
}

uint8_t
tiku_onewire_read_bit(void)
{
    return tiku_onewire_arch_read_bit();
}

void
tiku_onewire_write_byte(uint8_t byte)
{
    tiku_onewire_arch_write_byte(byte);
}

uint8_t
tiku_onewire_read_byte(void)
{
    return tiku_onewire_arch_read_byte();
}

#endif /* TIKU_BOARD_OW_AVAILABLE */
