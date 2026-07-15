/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_apollo510.c - Apollo510 carved-MRAM region backend (B).
 *
 * Mirrors the apollo4l backend over the linker-carved MRAM span
 * (__tiku_nvmfs_base / __tiku_nvmfs_size in apollo510.ld), with the Apollo5
 * deltas: the bootrom programmer nv_program_main2 lives at 0x0200ff20, the MRAM
 * array origin is 0x00400000 (so dst word offset = (addr - 0x400000) >> 2), and
 * the Cortex-M55 has an L1 D-cache -- the SSRAM staging buffer is cleaned before
 * the bootrom reads it and the programmed MRAM page is invalidated after, so
 * same-session reads see fresh data. MRAM is direct-write (no erase). Callers
 * already hold the NVM unlock window (tiku_mpu_unlock_nvm()).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"
#include <hal/tiku_cpu.h>      /* tiku_cpu_dcache_clean / _invalidate */

extern uint8_t __tiku_nvmfs_base;   /* region base (memory-mapped MRAM) */
extern uint8_t __tiku_nvmfs_size;   /* absolute symbol: its ADDRESS == size */

#define AMBIQ_MRAM_BASE         0x00400000UL  /* AM_HAL_MRAM_ADDR origin       */
#define AMBIQ_MRAM_PROGRAM_KEY  0x12344321UL
#define AMBIQ_MRAM_OP_PROGRAM   1U

typedef int (*nv_program_main2_t)(uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t);
#define NV_PROGRAM_MAIN2  ((nv_program_main2_t)(0x0200ff20UL + 1UL))

/* 16-byte-aligned read-modify-program staging window. In SSRAM (cached on the
 * M55) and cleaned before each program, matching the mirror flush. */
#define NVMR_STAGE_BYTES  256U
static uint32_t nvmr_stage[NVMR_STAGE_BYTES / 4U]
    __attribute__((section(".ssram"), aligned(16)));

static int mram_program_span(uintptr_t dst_addr, const uint32_t *src16,
                             size_t len)
{
    uint32_t primask;
    uint32_t dst_word_off = (uint32_t)((dst_addr - AMBIQ_MRAM_BASE) >> 2);
    int rc;

    /* Make the staging bytes visible to the bootrom's MRAM programmer. */
    tiku_cpu_dcache_clean((const void *)src16, len);

    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    __asm__ volatile ("cpsid i" ::: "memory");
    rc = NV_PROGRAM_MAIN2(AMBIQ_MRAM_PROGRAM_KEY, AMBIQ_MRAM_OP_PROGRAM,
                          (uint32_t)(uintptr_t)src16, dst_word_off,
                          (uint32_t)(len / 4U));
    __asm__ volatile ("msr primask, %0" : : "r"(primask) : "memory");

    /* Drop any cached copies of the freshly-programmed page. */
    tiku_cpu_dcache_invalidate((const void *)dst_addr, len);

    /* Bootrom status: 0 = programmed.  Propagate failures (previously
     * discarded) so callers' gate-last / CRC commits fail closed. */
    return rc;
}

static int region_write(tiku_nvm_backend_t *be, size_t off,
                        const void *src, size_t len)
{
    const uint8_t *s = (const uint8_t *)src;
    size_t span, aligned_start, aligned_end;

    if (off > be->size || len > be->size - off) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    aligned_start = off & ~((size_t)15U);
    aligned_end   = (off + len + 15U) & ~((size_t)15U);

    for (span = aligned_start; span < aligned_end; ) {
        size_t chunk = aligned_end - span;
        size_t ov_start, ov_end;

        if (chunk > NVMR_STAGE_BYTES) {
            chunk = NVMR_STAGE_BYTES;
        }
        memcpy(nvmr_stage, be->base + span, chunk);

        ov_start = (off > span) ? off : span;
        ov_end   = (off + len < span + chunk) ? (off + len) : (span + chunk);
        if (ov_end > ov_start) {
            memcpy((uint8_t *)nvmr_stage + (ov_start - span),
                   s + (ov_start - off), ov_end - ov_start);
        }
        if (mram_program_span((uintptr_t)(be->base + span), nvmr_stage,
                              chunk) != 0) {
            return -1;
        }
        span += chunk;
    }
    return 0;
}

static tiku_nvm_backend_t the_region;

const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    if (the_region.write == NULL) {
        the_region.base  = &__tiku_nvmfs_base;
        the_region.size  = (size_t)(uintptr_t)&__tiku_nvmfs_size;
        the_region.write = region_write;
        the_region.erase = NULL;
        the_region.ctx   = NULL;
    }
    return &the_region;
}
