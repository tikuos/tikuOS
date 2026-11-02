/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - nRF54L memory HAL (RRAM read/write).
 *
 * RRAM is memory-mapped and byte-writable with no erase cycle: a read is a
 * plain copy, and a write opens the RRAMC WEN gate, copies, waits for READY
 * and restores the gate to its previous state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_mem_hal.h>
#include <hal/tiku_mpu_hal.h>
#include <arch/nordic/tiku_nordic_mdk.h>
#include "kernel/memory/tiku_nvm_meter.h"
#include <string.h>

#define TIKU_RRAMC_READY_BIT   (1UL << 0)   /* RRAMC.READY: 1 = idle/ready */

/* Bytes metered as one operation: a few milliseconds of byte stores, inside
 * the 65 ms period of the 16-bit htimer the meter times them with. */
#define NORDIC_METER_PIECE     256u

/* The persist partition (nrf54l15.ld / nrf54lm20a.ld); .persistent is in it. */
extern const uint8_t __tiku_nvm_rram_start[];
extern const uint8_t __tiku_nvm_rram_end[];
extern uint8_t __persistent_start[];
extern uint8_t __persistent_end[];
extern uint32_t __sram_start;
extern uint32_t __sram_end;

void tiku_mem_arch_init(void)
{
    /* WRITEBUFSIZE = 0: each store commits directly to RRAM and no write
     * buffer holds data back. */
    uint32_t cfg = NRF_RRAMC_S->CONFIG;
    cfg &= ~(0x3FUL << 8);      /* clear WRITEBUFSIZE (bits 8..13) -> 0       */
    NRF_RRAMC_S->CONFIG = cfg;
}

const uint8_t *tiku_mem_arch_durable(size_t *len)
{
    /* Durable variables live in place in the RRAM persist partition. */
    *len = (size_t)(__tiku_nvm_rram_end - __tiku_nvm_rram_start);
    return __tiku_nvm_rram_start;
}

uint8_t *tiku_mem_arch_durable_live(size_t *len)
{
    /* .persistent sits at the start of the persist partition. */
    *len = (size_t)(__persistent_end - __persistent_start);
    return __persistent_start;
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
    volatile uint8_t *d = (volatile uint8_t *)dst;
    uint16_t gate;
    tiku_mem_arch_size_t i = 0u;
    int in_rram;

    if (dst == NULL || src == NULL) {
        return;
    }
    /* The HAL also writes TIKU_RETAINED data in SRAM (the shell history);
     * only a store outside SRAM programs RRAM. */
    in_rram = !((uintptr_t)dst >= (uintptr_t)&__sram_start &&
                (uintptr_t)dst < (uintptr_t)&__sram_end);

    gate = tiku_mpu_arch_unlock_nvm();      /* RRAMC WEN = 1 */

    /* With WRITEBUFSIZE 0 each store is a write of its own, and the RRAMC
     * programs the whole 128-bit wordline for it: one program a byte. */
    while (i < len) {
        tiku_mem_arch_size_t n = len - i;
        tiku_mem_arch_size_t k;
        tiku_nvm_mark_t t0;

        if (n > NORDIC_METER_PIECE) {
            n = NORDIC_METER_PIECE;
        }
        t0 = tiku_nvm_meter_begin();
        for (k = 0u; k < n; k++) {
            d[i + k] = src[i + k];
        }
        /* Wait for the last commit before closing the gate. */
        while ((NRF_RRAMC_S->READY & TIKU_RRAMC_READY_BIT) == 0UL) {
            /* spin until RRAMC is ready */
        }
        if (in_rram) {
            tiku_nvm_meter_end(t0, (uint32_t)n, (uint32_t)n, 0u);
        }
        i += n;
    }

    tiku_mpu_arch_lock_nvm(gate);           /* restore WEN */
}

int tiku_mem_arch_nvm_flush_status(void)
{
    /* RRAM writes are unbuffered (WRITEBUFSIZE = 0, set in tiku_mem_arch_init)
     * and nvm_write waits on RRAMC.READY, so nothing is held back; this waits
     * for the controller to be idle. */
    while ((NRF_RRAMC_S->READY & TIKU_RRAMC_READY_BIT) == 0UL) {
        /* spin until RRAMC is ready */
    }
    return 0;
}

void tiku_mem_arch_nvm_flush(void)
{
    (void)tiku_mem_arch_nvm_flush_status();
}
