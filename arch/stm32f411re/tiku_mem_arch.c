/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_mem_arch.c - STM32F411RE memory operations
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mem_arch.h"
#include <stdint.h>

void tiku_mem_arch_init(void)
{
    /* .persistent lives in SRAM .uninit on this first port, so there is
     * nothing to restore here yet. Warm-reset survival comes from the
     * linker NOLOAD placement; full power-loss persistence still needs a
     * flash mirror implementation. */
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)buf;
    tiku_mem_arch_size_t i;

    for (i = 0; i < len; i++) {
        p[i] = 0U;
    }
}

void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len)
{
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len)
{
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

void tiku_mem_arch_nvm_flush(void)
{
    /* Full flash-backed persistence is not implemented yet on STM32F411RE.
     * The live .persistent working copy remains in SRAM .uninit. */
}
