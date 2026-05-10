/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - RP2350 1-Wire stub (bit-bang TODO)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_onewire_arch.h"

int     tiku_onewire_arch_init(void)         { return TIKU_OW_OK; }
void    tiku_onewire_arch_close(void)        { /* nothing */ }
int     tiku_onewire_arch_reset(void)        { return TIKU_OW_ERR_NO_DEVICE; }
void    tiku_onewire_arch_write_bit(uint8_t b) { (void)b; }
uint8_t tiku_onewire_arch_read_bit(void)     { return 1U; }
void    tiku_onewire_arch_write_byte(uint8_t b) { (void)b; }
uint8_t tiku_onewire_arch_read_byte(void)    { return 0xFFU; }
