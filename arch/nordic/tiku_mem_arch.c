/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - nRF54L memory HAL (RRAM read/write)
 *
 * RRAM is memory-mapped and byte-writable in place (no erase cycle), so:
 *   read  -- a plain copy (RRAM reads like SRAM).
 *   write -- open the RRAMC WEN gate (tiku_mpu_arch_unlock_nvm), copy the
 *            bytes straight into RRAM, wait for the controller to report
 *            ready, then restore the gate.
 * This mirrors the MSP430 FRAM writer.  Callers that already hold the WEN
 * window (persist cells) still work: unlock/lock save+restore the prior gate
 * state, so the nesting is correct.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_mem_hal.h>
#include <hal/tiku_mpu_hal.h>
#include <arch/nordic/tiku_nordic_mdk.h>
#include <string.h>

#define TIKU_RRAMC_READY_BIT   (1UL << 0)   /* RRAMC.READY: 1 = idle/ready */

void tiku_mem_arch_init(void)
{
    /* Unbuffered writes: each store commits directly to RRAM (no write buffer
     * to flush), which keeps the nvm_write path simple.  WRITEBUFSIZE = 0. */
    uint32_t cfg = NRF_RRAMC_S->CONFIG;
    cfg &= ~(0x3FUL << 8);      /* clear WRITEBUFSIZE (bits 8..13) -> 0       */
    NRF_RRAMC_S->CONFIG = cfg;
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)buf;
    tiku_mem_arch_size_t i;

    if (buf == NULL) {
        return;
    }
    for (i = 0u; i < len; i++) {
        p[i] = 0u;
    }
    __asm__ volatile ("" ::: "memory");   /* barrier: keep the wipe */
}

void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    /* RRAM is directly readable. */
    memcpy(dst, src, (size_t)len);
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len)
{
    uint16_t gate;
    tiku_mem_arch_size_t i;

    if (dst == NULL || src == NULL) {
        return;
    }

    gate = tiku_mpu_arch_unlock_nvm();      /* RRAMC WEN = 1 */

    for (i = 0u; i < len; i++) {
        dst[i] = src[i];                    /* byte write straight into RRAM */
    }

    /* Wait for the controller to finish committing before closing the gate. */
    while ((NRF_RRAMC_S->READY & TIKU_RRAMC_READY_BIT) == 0UL) {
        /* spin until RRAMC is ready */
    }

    tiku_mpu_arch_lock_nvm(gate);           /* restore WEN */
}

void tiku_mem_arch_nvm_flush(void)
{
    /* RRAM writes are unbuffered (WRITEBUFSIZE = 0 in tiku_mem_arch_init) and
     * nvm_write already spins on RRAMC.READY, so there is no deferred write
     * buffer to drain -- this exists to satisfy the HAL and simply makes sure
     * the controller is idle. */
    while ((NRF_RRAMC_S->READY & TIKU_RRAMC_READY_BIT) == 0UL) {
        /* spin until RRAMC is ready */
    }
}
