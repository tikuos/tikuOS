/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - Apollo 510 1-Wire driver (stub)
 *
 * Not yet supported. A GPIO bit-bang implementation lands with the
 * peripheral pass once the htimer microsecond source is wired.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_onewire_arch.h"

int tiku_onewire_arch_init(void) {
    return -1;
}

void tiku_onewire_arch_close(void) {
}

int tiku_onewire_arch_reset(void) {
    return -1;
}

void tiku_onewire_arch_write_bit(uint8_t bit) {
    (void)bit;
}

uint8_t tiku_onewire_arch_read_bit(void) {
    return 1;
}

void tiku_onewire_arch_write_byte(uint8_t byte) {
    (void)byte;
}

uint8_t tiku_onewire_arch_read_byte(void) {
    return 0xFF;
}
