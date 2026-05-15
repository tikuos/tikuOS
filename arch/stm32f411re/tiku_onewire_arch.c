/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_onewire_arch.c - STM32F411RE 1-Wire compatibility backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_onewire_arch.h"
#include <stdint.h>

int tiku_onewire_arch_init(void)
{
    return TIKU_OW_OK;
}

void tiku_onewire_arch_close(void)
{
}

int tiku_onewire_arch_reset(void)
{
    return TIKU_OW_ERR_NO_DEVICE;
}

void tiku_onewire_arch_write_bit(uint8_t bit)
{
    (void)bit;
}

uint8_t tiku_onewire_arch_read_bit(void)
{
    return 1U;
}

void tiku_onewire_arch_write_byte(uint8_t byte)
{
    (void)byte;
}

uint8_t tiku_onewire_arch_read_byte(void)
{
    return 0xFFU;
}
