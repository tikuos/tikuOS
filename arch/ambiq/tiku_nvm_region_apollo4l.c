/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_apollo4l.c - Apollo4 Lite carved-MRAM region backend.
 *
 * Reads dereference the memory-mapped region.  Writes go through the bootrom
 * in whole words from an aligned source: tiku_nvm_mram_program() stages,
 * overlays and programs each span; region_write() needs an NVM unlock window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"
#include "tiku_mem_arch.h"     /* tiku_nvm_mram_program prototype */
#include <hal/tiku_cpu.h>      /* tiku_cpu_dcache_invalidate (CACHECTRL) */

/*---------------------------------------------------------------------------*/
/* LINKER-CARVED REGION BOUNDS (apollo4l.ld)                                 */
/*---------------------------------------------------------------------------*/

extern uint8_t __tiku_nvmfs_base;   /* region base (memory-mapped MRAM)      */
extern uint8_t __tiku_nvmfs_size;   /* absolute symbol: its address is size  */

/*---------------------------------------------------------------------------*/
/* BOOTROM MRAM PROGRAMMER (as in tiku_mem_apollo4l.c)                       */
/*---------------------------------------------------------------------------*/

#define AMBIQ_MRAM_BASE         0x00000000UL  /* word-offset origin           */
#define AMBIQ_MRAM_PROGRAM_KEY  0x12344321UL  /* AM_HAL_MRAM_PROGRAM_KEY      */
#define AMBIQ_MRAM_OP_PROGRAM   1U            /* program main array           */

/* nv_program_main2(key, op, src_addr, dst_word_offset, num_words). The stored
 * value already carries the Thumb bit (bit 0). */
typedef int (*nv_program_main2_t)(uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t);
#define NV_PROGRAM_MAIN2  ((nv_program_main2_t)0x0800006DUL)

/* 16-byte-aligned read-modify-program staging window in TCM (.bss). 256 B
 * bounds the per-program size; tiku_nvm_mram_program() chunks larger writes
 * through it.  256 is a multiple of 16, so every programmed span stays
 * 16-byte aligned. */
#define NVMR_STAGE_BYTES  256U
static uint32_t nvmr_stage[NVMR_STAGE_BYTES / 4U] __attribute__((aligned(16)));

/**
 * @brief Program one 16-byte-aligned, 16-byte-multiple span into MRAM.
 *
 * @param dst_addr  Absolute MRAM byte address (16-byte aligned).
 * @param src16     16-byte-aligned source (the staging buffer).
 * @param len       Bytes to program (multiple of 16).
 * @return 0 on success (bootrom status), non-zero on programming failure.
 */
static int mram_program_span(uintptr_t dst_addr, const uint32_t *src16,
                             size_t len)
{
    uint32_t primask;
    uint32_t dst_word_off = (uint32_t)((dst_addr - AMBIQ_MRAM_BASE) >> 2);
    int rc;

    /* MRAM program is uninterruptible: an ISR fetch could fault mid-program. */
    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    __asm__ volatile ("cpsid i" ::: "memory");
    rc = NV_PROGRAM_MAIN2(AMBIQ_MRAM_PROGRAM_KEY, AMBIQ_MRAM_OP_PROGRAM,
                          (uint32_t)(uintptr_t)src16, dst_word_off,
                          (uint32_t)(len / 4U));
    __asm__ volatile ("msr primask, %0" : : "r"(primask) : "memory");

    /* Bootrom status: 0 = programmed.  A failure is returned to the caller,
     * as on the Apollo510 backend. */
    return rc;
}

int tiku_nvm_mram_program(uintptr_t dst, const void *src, size_t len)
{
    const uint8_t *s = (const uint8_t *)src;
    uintptr_t span, aligned_start, aligned_end;

    /* The boot/info area occupies the low 96 KB; MRAM ends at 0x200000
     * (2 MB). */
    if (dst < 0x00018000UL ||
        dst > 0x00200000UL || len > 0x00200000UL - dst) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    aligned_start = dst & ~((uintptr_t)15U);
    aligned_end   = (dst + len + 15U) & ~((uintptr_t)15U);

    for (span = aligned_start; span < aligned_end; ) {
        size_t chunk = aligned_end - span;
        uintptr_t ov_start, ov_end;

        if (chunk > NVMR_STAGE_BYTES) {
            chunk = NVMR_STAGE_BYTES;
        }

        /* Preserve the existing MRAM bytes in this window, then overlay the
         * portion of [dst, dst+len) that falls inside it. */
        memcpy(nvmr_stage, (const void *)span, chunk);

        ov_start = (dst > span) ? dst : span;
        ov_end   = (dst + len < span + chunk) ? (dst + len) : (span + chunk);
        if (ov_end > ov_start) {
            memcpy((uint8_t *)nvmr_stage + (ov_start - span),
                   s + (ov_start - dst),
                   ov_end - ov_start);
        }

        if (mram_program_span(span, nvmr_stage, chunk) != 0) {
            return -1;
        }
        span += chunk;
    }

    /* Drop stale cached copies of the programmed range.  The Apollo4
     * CACHECTRL has no by-range operation: the arguments are ignored and
     * the whole cache is invalidated.  The cache is unified, so instruction
     * fetches see the new bytes too. */
    tiku_cpu_dcache_invalidate((const void *)dst, len);
    return 0;
}

/**
 * @brief Backend write: read-modify-program @p len bytes at @p off.
 *
 * Thin wrapper over tiku_nvm_mram_program (bounds-check + offset->address).
 * Must be called inside an NVM unlock window.
 *
 * @return 0 on success, -1 if out of bounds or the bootrom reports failure.
 */
static int region_write(tiku_nvm_backend_t *be, size_t off,
                        const void *src, size_t len)
{
    if (off > be->size || len > be->size - off) {
        return -1;                      /* out of range */
    }
    return tiku_nvm_mram_program((uintptr_t)(be->base + off), src, len);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC ACCESSOR                                                           */
/*---------------------------------------------------------------------------*/

static tiku_nvm_backend_t the_region;

const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    if (the_region.write == NULL) {
        the_region.base  = &__tiku_nvmfs_base;
        the_region.size  = (size_t)(uintptr_t)&__tiku_nvmfs_size;
        the_region.write = region_write;
        the_region.erase = NULL;        /* MRAM is byte-addressable, no erase */
        the_region.ctx   = NULL;
    }
    return &the_region;
}
