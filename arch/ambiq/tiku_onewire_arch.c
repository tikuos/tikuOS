/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - Ambiq 1-Wire driver, a stub.
 *
 * This port has no 1-Wire driver: init and reset return -1, the reads return
 * an idle bus (bit 1, byte 0xFF), and the writes and close do nothing.
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
