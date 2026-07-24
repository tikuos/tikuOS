/*
 * Tiku Operating System v0.06
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
