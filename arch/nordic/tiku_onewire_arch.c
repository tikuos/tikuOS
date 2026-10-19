/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - nRF54L 1-Wire backend (stub).
 *
 * This port has no 1-Wire driver: init and reset return -1
 * (TIKU_OW_ERR_NO_DEVICE), writes do nothing, a bit read returns 1 and a byte
 * read 0xFF, the levels of an idle pulled-up line.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_onewire_arch.h>

int tiku_onewire_arch_init(void)
{
    return -1;
}

void tiku_onewire_arch_close(void)
{
}

int tiku_onewire_arch_reset(void)
{
    return -1;
}

void tiku_onewire_arch_write_bit(uint8_t bit)
{
    (void)bit;
}

uint8_t tiku_onewire_arch_read_bit(void)
{
    return 1;
}

void tiku_onewire_arch_write_byte(uint8_t byte)
{
    (void)byte;
}

uint8_t tiku_onewire_arch_read_byte(void)
{
    return 0xFF;
}
