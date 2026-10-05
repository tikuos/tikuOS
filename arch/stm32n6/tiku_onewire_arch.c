/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - STM32N6 1-Wire stub.
 *
 * This port has no 1-Wire driver: init returns TIKU_OW_ERR_PARAM, reset
 * returns TIKU_OW_ERR_NO_DEVICE, writes do nothing, and reads return the
 * idle-high bus, 1 for a bit and 0xFF for a byte.
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
