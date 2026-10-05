/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - RA8P1 memory arch hooks.
 *
 * `.persistent` lives in MRAM, which is byte-writable in place, so reads and
 * writes are plain memory accesses.  A commit flushes the controller's
 * 32-byte write buffer into the array.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mem_arch.h"
#include "tiku_mram_arch.h"

/** @brief Successful flushes, for tiku_mem_arch_nvm_program_count(). */
static uint32_t ra8p1_nvm_programs;

/* The persist partition (r7ka8p1kf.ld); .persistent is in it. */
extern const uint8_t __tiku_nvm_mram_start[];
extern const uint8_t __tiku_nvm_mram_end[];
extern uint8_t __persistent_start[];
extern uint8_t __persistent_end[];

void tiku_mem_arch_init(void)
{
    /* Nothing to unlock here: the MRAM programming gate is opened per write
     * window by tiku_mpu_arch_unlock_nvm(), so it is shut whenever no durable
     * write is in progress. */
}

/** @brief The persist partition: durable variables live there in place. */
const uint8_t *tiku_mem_arch_durable(size_t *len)
{
    *len = (size_t)(__tiku_nvm_mram_end - __tiku_nvm_mram_start);
    return __tiku_nvm_mram_start;
}

/** @brief .persistent, which opens the partition (see tiku_mem_hal.h). */
uint8_t *tiku_mem_arch_durable_live(size_t *len)
{
    *len = (size_t)(__persistent_end - __persistent_start);
    return __persistent_start;
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len)
{
    volatile uint8_t *p = buf;

    if (buf == NULL) {
        return;
    }
    /* Volatile stores: the compiler can delete a plain wipe of a buffer that
     * is not read again. */
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

int tiku_mem_arch_nvm_flush_status(void)
{
    /* Flush the controller's 32-byte write buffer into the array; until then
     * a store reads back from the buffer, and a power cut loses it. */
    if (tiku_ra8p1_mram_flush() == TIKU_RA8P1_MRAM_OK) {
        ra8p1_nvm_programs++;
        return 0;
    }
    return -1;
}

/** @brief tiku_mem_arch_nvm_flush_status() with the result discarded. */
void tiku_mem_arch_nvm_flush(void)
{
    (void)tiku_mem_arch_nvm_flush_status();
}

uint32_t tiku_mem_arch_nvm_program_count(void)
{
    return ra8p1_nvm_programs;
}
