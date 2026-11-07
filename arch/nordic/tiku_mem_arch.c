/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - nRF54L memory HAL (RRAM read/write).
 *
 * RRAM is memory-mapped with no erase cycle: a read is a plain copy, and a
 * write stores each changed 16-byte line through the RRAMC write buffer
 * inside the WEN gate, then commits the buffer and waits for READY.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_mem_hal.h>
#include <hal/tiku_mpu_hal.h>
#include <arch/nordic/tiku_nordic_mdk.h>
#include "kernel/memory/tiku_nvm_media.h"
#include "kernel/memory/tiku_nvm_meter.h"
#include <string.h>

#define TIKU_RRAMC_READY_BIT   (1UL << 0)   /* RRAMC.READY: 1 = idle/ready */
#define TIKU_RRAMC_EMPTY_BIT   (1UL << 0)   /* BUFSTATUS.WRITEBUFEMPTY: 1 = empty */

/* CONFIG.WRITEBUFSIZE: the 128-bit lines the write buffer collects before a
 * commit, at most RRAMC_CONFIG_WRITEBUFSIZE_Max (32; 16 on the nRF54LV10A);
 * 0 programs a line for every store.  A commit programs each line in the
 * buffer whose data changed, once. */
#define RRAMC_WRITEBUF_POS     8u
#define RRAMC_WRITEBUF_MSK     (0x3FUL << RRAMC_WRITEBUF_POS)
#define RRAMC_WRITEBUF_LINES   RRAMC_CONFIG_WRITEBUFSIZE_Max

/* The 128-bit line the RRAMC programs as one unit. */
#define RRAM_LINE              TIKU_NVM_MEDIA_UNIT

/* Lines committed as one metered operation: no more than the smallest
 * buffer holds (16 lines on the nRF54LV10A), and well inside the 65 ms period
 * of the 16-bit htimer the meter times them with. */
#define NORDIC_METER_LINES     16u

/* The persist partition (nrf54l15.ld / nrf54lm20a.ld); .persistent is in it. */
extern const uint8_t __tiku_nvm_rram_start[];
extern const uint8_t __tiku_nvm_rram_end[];
extern uint8_t __persistent_start[];
extern uint8_t __persistent_end[];
extern uint32_t __sram_start;
extern uint32_t __sram_end;

void tiku_mem_arch_init(void)
{
    /* WRITEBUFSIZE = 0: a store outside the write HAL commits on its own;
     * the HAL turns the buffer on for its own stores and off again. */
    uint32_t cfg = NRF_RRAMC_S->CONFIG;
    cfg &= ~RRAMC_WRITEBUF_MSK;
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

/** @brief Commit the write buffer, if it holds data, and wait for RRAMC. */
static void rramc_commit(void)
{
    __asm__ volatile ("dsb" ::: "memory");  /* every store is in the buffer */
    if ((NRF_RRAMC_S->BUFSTATUS.WRITEBUFEMPTY & TIKU_RRAMC_EMPTY_BIT) == 0UL) {
        NRF_RRAMC_S->TASKS_COMMITWRITEBUF = 1UL;
    }
    while ((NRF_RRAMC_S->READY & TIKU_RRAMC_READY_BIT) == 0UL) {
        /* spin until the commit is done */
    }
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    uint16_t gate;
    uint32_t cfg;
    tiku_mem_arch_size_t i = 0u;
    int in_rram;

    if (dst == NULL || src == NULL) {
        return;
    }
    /* The HAL also writes TIKU_RETAINED data in SRAM (the shell history):
     * a plain copy that programs nothing. */
    in_rram = !((uintptr_t)dst >= (uintptr_t)&__sram_start &&
                (uintptr_t)dst < (uintptr_t)&__sram_end);

    gate = tiku_mpu_arch_unlock_nvm();      /* RRAMC WEN = 1 */
    cfg = NRF_RRAMC_S->CONFIG;
    if (in_rram) {
        NRF_RRAMC_S->CONFIG = (cfg & ~RRAMC_WRITEBUF_MSK) |
                              (RRAMC_WRITEBUF_LINES << RRAMC_WRITEBUF_POS);
    }

    while (i < len) {
        tiku_nvm_mark_t t0 = tiku_nvm_meter_begin();
        uint32_t bytes = 0u, lines = 0u, seen = 0u;

        /* One piece, in address order: a line's bytes are stored only where
         * they differ from what the line holds. */
        while (i < len && seen < NORDIC_METER_LINES) {
            tiku_mem_arch_size_t n = (tiku_mem_arch_size_t)
                (RRAM_LINE - (((uintptr_t)dst + i) & (RRAM_LINE - 1u)));
            tiku_mem_arch_size_t k;

            if (n > len - i) {
                n = len - i;
            }
            if (!in_rram || memcmp(dst + i, src + i, (size_t)n) != 0) {
                for (k = 0u; k < n; k++) {
                    d[i + k] = src[i + k];
                }
                bytes += n;
                lines++;
            }
            i += n;
            seen++;
        }
        if (in_rram && lines != 0u) {
            rramc_commit();
            tiku_nvm_meter_end(t0, bytes, lines, 0u);
        }
    }

    if (in_rram) {
        /* The buffer as it was (off, from tiku_mem_arch_init()). */
        NRF_RRAMC_S->CONFIG = (NRF_RRAMC_S->CONFIG & ~RRAMC_WRITEBUF_MSK) |
                              (cfg & RRAMC_WRITEBUF_MSK);
    }
    tiku_mpu_arch_lock_nvm(gate);           /* restore WEN */
}

int tiku_mem_arch_nvm_flush_status(void)
{
    /* The write HAL commits its buffer and waits for READY before it
     * returns, and every other store is unbuffered (WRITEBUFSIZE = 0 outside
     * the HAL), so nothing is held back; this waits for the controller to be
     * idle. */
    while ((NRF_RRAMC_S->READY & TIKU_RRAMC_READY_BIT) == 0UL) {
        /* spin until RRAMC is ready */
    }
    return 0;
}

void tiku_mem_arch_nvm_flush(void)
{
    (void)tiku_mem_arch_nvm_flush_status();
}
