/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - Apollo 510 memory architecture support + MRAM-backed NVM
 *
 * Persistent state lives in the SRAM .uninit region (warm-reset durable). For
 * power-cycle durability it is mirrored to a 32 KB MRAM page reserved at the top
 * of MRAM (apollo510.ld __tiku_nvm_mram_*). tiku_mem_arch_nvm_flush() snapshots
 * .uninit (prefixed with a magic word) and programs the page via the on-chip
 * bootrom (nv_program_main2 -- MRAM is direct-write, NO erase, unlike NOR
 * flash). On boot, tiku_mem_arch_init() copies the page back into .uninit if the
 * magic matches; otherwise (fresh chip) .uninit keeps its NOLOAD value and each
 * subsystem's "no magic -> init fresh" logic runs.
 *
 * Mirrors the RP2350 flash-mirror design (arch/arm-rp2350/tiku_mem_arch.c),
 * substituting the MRAM bootrom for the QSPI boot-ROM, plus D-cache maintenance
 * around the SSRAM snapshot and the programmed page (Apollo5 has L1 cache). The
 * reserved page is far above the firmware code; the SBL (0x400000) is untouched.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include "tiku_mem_arch.h"
#include "apollo510.h"   /* SCB_{Clean,Invalidate}DCache_by_Addr (core_cm55.h) */

/* Live .uninit working copy + the reserved MRAM mirror page (apollo510.ld). */
extern uint8_t  __uninit_start;
extern uint8_t  __uninit_end;
extern uint32_t __tiku_nvm_mram_start;   /* base address of the mirror page */

#define AMBIQ_MRAM_BASE         0x00400000UL  /* AM_HAL_MRAM_ADDR (word-offset origin) */
#define AMBIQ_MRAM_PROGRAM_KEY  0x12344321UL  /* AM_HAL_MRAM_PROGRAM_KEY */
#define AMBIQ_MRAM_OP_PROGRAM   1U            /* AM_HAL_MRAM_PROGRAM */
#define TIKU_NVM_MAGIC          0x4E564D54U   /* 'NVMT' little-endian */
#define TIKU_NVM_MRAM_BYTES     0x8000U       /* MUST match __tiku_nvm_mram_size */

/* On-chip bootrom MRAM programmer (g_am_hal_bootrom_helper.nv_program_main2),
 * fixed ROM entry, Thumb bit set:
 *   nv_program_main2(key, op, src_addr, dst_word_offset, num_words). */
typedef int (*nv_program_main2_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#define NV_PROGRAM_MAIN2  ((nv_program_main2_t)(0x0200ff20UL + 1UL))

/* Page-sized snapshot in SSRAM (3 MB available), 16-byte aligned for the MRAM
 * programmer. Layout: word[0] = magic, words[1..] = .uninit image, 0xFF pad. */
static uint32_t g_nvm_snap[TIKU_NVM_MRAM_BYTES / 4U]
    __attribute__((section(".ssram"), aligned(16)));

static size_t uninit_bytes(void) {
    return (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);
}

void tiku_mem_arch_init(void) {
    /* Restore .uninit from the MRAM mirror if it carries our magic. On a fresh
     * boot the cache is empty (or off) here, so reading the page sees the
     * current MRAM contents. */
    const uint32_t *mirror = (const uint32_t *)&__tiku_nvm_mram_start;
    if (mirror[0] == TIKU_NVM_MAGIC) {
        size_t n = uninit_bytes();
        if (n > (TIKU_NVM_MRAM_BYTES - 4U)) {
            n = TIKU_NVM_MRAM_BYTES - 4U;
        }
        memcpy(&__uninit_start, (const uint8_t *)&mirror[1], n);
    }
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    volatile uint8_t *p = buf;
    while (len--) {
        *p++ = 0u;
    }
}

void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len) {
    memcpy(dst, src, len);   /* .uninit NVM stand-in is memory-mapped SRAM */
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    /* SRAM working copy; the MRAM commit happens at the matching
     * tiku_mpu_lock_nvm() relock via tiku_mem_arch_nvm_flush(), so direct
     * stores into .persistent inside the unlock window are captured too. */
    memcpy(dst, src, len);
}

void tiku_mem_arch_nvm_flush(void) {
    size_t   n = uninit_bytes();
    size_t   snap_bytes, prog_bytes;
    uint32_t dst_word_off, primask;

    if (n > (TIKU_NVM_MRAM_BYTES - 4U)) {
        n = TIKU_NVM_MRAM_BYTES - 4U;
    }

    /* Compose: magic word, then the .uninit image, padded to a 16-byte unit. */
    g_nvm_snap[0] = TIKU_NVM_MAGIC;
    memcpy((uint8_t *)&g_nvm_snap[1], &__uninit_start, n);
    snap_bytes = 4U + n;
    prog_bytes = (snap_bytes + 15U) & ~((size_t)15U);
    if (prog_bytes > TIKU_NVM_MRAM_BYTES) {
        prog_bytes = TIKU_NVM_MRAM_BYTES;
    }
    if (prog_bytes > snap_bytes) {
        memset((uint8_t *)g_nvm_snap + snap_bytes, 0xFFu, prog_bytes - snap_bytes);
    }

    /* Write the snapshot back to SSRAM so the MRAM programmer reads it there. */
    SCB_CleanDCache_by_Addr((void *)g_nvm_snap, (int32_t)prog_bytes);

    dst_word_off =
        ((uint32_t)(uintptr_t)&__tiku_nvm_mram_start - AMBIQ_MRAM_BASE) >> 2;

    /* MRAM program is uninterruptible (an ISR fetch could fault mid-program). */
    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    __asm__ volatile ("cpsid i" ::: "memory");
    (void)NV_PROGRAM_MAIN2(AMBIQ_MRAM_PROGRAM_KEY, AMBIQ_MRAM_OP_PROGRAM,
                           (uint32_t)(uintptr_t)g_nvm_snap, dst_word_off,
                           (uint32_t)(prog_bytes / 4U));
    __asm__ volatile ("msr primask, %0" : : "r"(primask) : "memory");

    /* Drop any cached copies of the freshly-programmed page so same-session
     * reads of the mirror see the new data. */
    SCB_InvalidateDCache_by_Addr((void *)&__tiku_nvm_mram_start, (int32_t)prog_bytes);
}
