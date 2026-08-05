/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - RA8P1 memory arch hooks.
 *
 * `.persistent` is real MRAM, byte-writable in place, so reads and writes are
 * plain memory access and there is no mirror.  A commit still has work to do:
 * push the controller's 32-byte write buffer into the array.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mem_arch.h"
#include "tiku_mram_arch.h"

/** @brief Programs counted since boot; see tiku_mem_arch_nvm_program_count(). */
static uint32_t ra8p1_nvm_programs;

void tiku_mem_arch_init(void)
{
    /* Nothing to unlock here: the MRAM programming gate is opened per write
     * window by tiku_mpu_arch_unlock_nvm(), so it is shut whenever no durable
     * write is in progress. */
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
    /* No mirror to copy -- but a real commit to make.  A store leaves its
     * bytes in the controller's 32-byte buffer and READS BACK from there, so
     * without this the caller cannot tell a durable write from a lost one. */
    if (tiku_ra8p1_mram_flush() == TIKU_RA8P1_MRAM_OK) {
        ra8p1_nvm_programs++;
    }
}

uint32_t tiku_mem_arch_nvm_program_count(void)
{
    return ra8p1_nvm_programs;
}
