/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - RA8P1 1-Wire, unimplemented.
 *
 * No hardware backend yet. Every call fails cleanly so a caller learns the
 * bus is absent instead of reading zeros as data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_onewire_arch.h"

int tiku_onewire_arch_init(void) {
    return TIKU_OW_ERR_PARAM;
}

void tiku_onewire_arch_close(void) {
}

int tiku_onewire_arch_reset(void) {
    return TIKU_OW_ERR_NO_DEVICE;
}

void tiku_onewire_arch_write_bit(uint8_t bit) {
    (void)bit;
}

uint8_t tiku_onewire_arch_read_bit(void) {
    return 1U;      /* the bus idles high */
}

void tiku_onewire_arch_write_byte(uint8_t byte) {
    (void)byte;
}

uint8_t tiku_onewire_arch_read_byte(void) {
    return 0xFFU;
}
