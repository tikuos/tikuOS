/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - STM32N6 memory helpers.
 *
 * With no internal NVM the durable calls are plain SRAM copies; they keep the
 * kernel's contract so the XSPI backend can replace them without callers changing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "tiku_mem_arch.h"

void tiku_mem_arch_init(void) {
    /* No NVM controller to unlock and no cache to configure for it. */
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    if (buf == NULL) {
        return;
    }
    /* Written through a volatile pointer so the compiler cannot drop a wipe
     * whose result is never read. */
    volatile uint8_t *p = buf;
    while (len-- > 0U) {
        *p++ = 0U;
    }
    __asm__ volatile ("dsb" ::: "memory");
}

void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len) {
    if (dst == NULL || src == NULL) {
        return;
    }
    while (len-- > 0U) {
        *dst++ = *src++;
    }
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    if (dst == NULL || src == NULL) {
        return;
    }
    while (len-- > 0U) {
        *dst++ = *src++;
    }
    __asm__ volatile ("dsb" ::: "memory");
}

void tiku_mem_arch_nvm_flush(void) {
    /* Nothing is buffered on the way to SRAM; order the writes and return. */
    __asm__ volatile ("dsb" ::: "memory");
}
