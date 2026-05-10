/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - RP2350 memory operations
 *
 * No real NVM driver in this port: tiku_mem_arch_nvm_{read,write}
 * is just memcpy-style. Persistent state declared with the
 * .persistent / .uninit attributes lives in the SRAM .uninit
 * section (see linker script) — it survives warm resets but a
 * power cycle starts from cold. A future revision can add a real
 * QSPI flash erase/program driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mem_arch.h"

void tiku_mem_arch_init(void) {
    /* Nothing to do — the linker script already sized SRAM correctly,
     * and there is no FRAM wait-state setup on this MCU. */
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)buf;
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        p[i] = 0U;
    }
}

void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len) {
    /* On MSP430 this hits memory-mapped FRAM. On RP2350 we treat the
     * "NVM" as plain SRAM for the first port (the .persistent section
     * is in the SRAM .uninit region). Programs that want true
     * non-volatile storage need a flash driver — TODO. */
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}
