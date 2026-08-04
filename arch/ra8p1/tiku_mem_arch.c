/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - RA8P1 memory arch hooks.
 *
 * The durable region is a linker section the reset handler's zero-fill skips,
 * so reads and writes are plain memory access and the "commit" has nothing to
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mem_arch.h"

#include <kernel/memory/tiku_nvm_mirror.h>

void tiku_mem_arch_init(void)
{
    /* Nothing to unlock: the region is ordinary SRAM until R6. */
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len)
{
    volatile uint8_t *p = buf;

    if (buf == NULL) {
        return;
    }
    /* volatile so the compiler cannot decide a wipe of a dead buffer is
     * unobservable and delete it -- which is exactly what it would do. */
    while (len-- > 0U) {
        *p++ = 0U;
    }
}

void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    while (len-- > 0U) {
        *dst++ = *src++;
    }
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    while (len-- > 0U) {
        *dst++ = *src++;
    }
}

void tiku_mem_arch_nvm_flush(void)
{
    /* Deliberately empty.  See the file comment: there is no mirror to flush,
     * and the program count below reports that honestly rather than counting
     * flushes that moved nothing. */
}

int tiku_mem_arch_nvm_restore_status(void)
{
    /* VIRGIN, always: nothing was restored because there is nowhere to restore
     * from.  Reporting V2_OK here would tell /sys/persist that durable state
     * survived a power cycle, which on this port it does not. */
    return TIKU_NVM_RESTORE_VIRGIN;
}

uint32_t tiku_mem_arch_nvm_program_count(void)
{
    return 0UL;
}
