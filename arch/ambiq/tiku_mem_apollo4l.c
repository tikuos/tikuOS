/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch_apollo4l.c - Apollo4 Lite memory architecture + MRAM-backed NVM
 *
 * Persistent state lives in the TCM .uninit region (warm-reset durable). For
 * power-cycle durability it is mirrored to a 32 KB MRAM page reserved at the top
 * of MRAM (apollo4l.ld __tiku_nvm_mram_*). tiku_mem_arch_nvm_flush() snapshots
 * .uninit (prefixed with a magic word) into a TCM staging buffer and programs
 * the page via the Apollo4 on-chip bootrom helper nv_program_main2. On boot,
 * tiku_mem_arch_init() copies the page back into .uninit if the magic matches;
 * a fresh chip has no magic, so .uninit keeps its NOLOAD value and each
 * subsystem's "no magic -> init fresh" path runs.
 *
 * Mirrors arch/ambiq/tiku_mem_arch.c (Apollo510), with the Apollo4 deltas:
 *   - bootrom helper nv_program_main2 lives at 0x0800006D (verified in the
 *     R4.5.0 g_am_hal_bootrom_helper table) -- a DIFFERENT address than
 *     Apollo5's 0x0200ff20. The value is already Thumb-encoded (bit 0 set).
 *   - MRAM array origin is 0x0 (so the destination word offset is addr >> 2),
 *     vs Apollo5's 0x00400000.
 *   - the Cortex-M4 has NO SCB L1 cache, so there is no clean/invalidate around
 *     the snapshot or the programmed page.
 *   - the staging buffer lives in the always-on TCM (.bss), not a separately
 *     powered SSRAM bank; 0x10000000 is a valid bootrom source (SRAM_BASEADDR).
 * MRAM is direct-write (no erase, unlike NOR flash). The reserved page is far
 * above the firmware code; the SBL (low MRAM) is untouched.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include "tiku_mem_arch.h"
#include <hal/tiku_cpu.h>   /* tiku_cpu_dcache_invalidate (D-cache coherency) */

/* Live .uninit working copy + the reserved MRAM mirror page (apollo4l.ld). */
extern uint8_t  __uninit_start;
extern uint8_t  __uninit_end;
extern uint32_t __tiku_nvm_mram_start;   /* base address of the mirror page */

/**
 * @defgroup MEM_ARCH_CONSTS Apollo4 Lite MRAM driver constants
 * @brief Fixed addresses, keys, and sizes for the bootrom MRAM programmer.
 * @{
 */
#define AMBIQ_MRAM_BASE         0x00000000UL  /* AM_HAL_MRAM_ADDR (word-offset origin) */
#define AMBIQ_MRAM_PROGRAM_KEY  0x12344321UL  /* AM_HAL_MRAM_PROGRAM_KEY */
#define AMBIQ_MRAM_OP_PROGRAM   1U            /* program main array (fill = 0) */
#define TIKU_NVM_MAGIC          0x4E564D54U   /* 'NVMT' little-endian */
#define TIKU_NVM_MRAM_BYTES     0x10000U      /* 64 KB; MUST match __tiku_nvm_mram_size */
/** @} */

/**
 * @brief On-chip bootrom MRAM programmer function type
 *
 * Fixed ROM entry from the AmbiqSuite R4.5.0 bootrom helper table
 * (g_am_hal_bootrom_helper.nv_program_main2, am_hal_bootrom_helper.c).
 * Signature: nv_program_main2(key, op, src_addr, dst_word_offset, num_words).
 * The stored value 0x0800006D already carries the Thumb bit (bit 0), so it is
 * used as-is. MRAM is direct-write (no erase required).
 */
typedef int (*nv_program_main2_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#define NV_PROGRAM_MAIN2  ((nv_program_main2_t)0x0800006DUL)

/**
 * @brief TCM staging buffer for MRAM programming
 *
 * Placed in the always-on TCM (.bss) and 16-byte aligned as the bootrom MRAM
 * programmer requires. Layout: word[0] = magic, words[1..] = .uninit image,
 * 0xFF padding to a 16-byte boundary.
 */
static uint32_t g_nvm_snap[TIKU_NVM_MRAM_BYTES / 4U]
    __attribute__((aligned(16)));

/** @brief Return the size of the .uninit region in bytes. */
static size_t uninit_bytes(void) {
    return (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);
}

/**
 * @brief Restore .uninit state from the MRAM mirror on boot
 *
 * Checks word[0] of the reserved MRAM page for TIKU_NVM_MAGIC. If it matches,
 * copies the stored .uninit image back into RAM, making power-cycle-durable
 * state visible to subsequent subsystem init. On a fresh chip the magic is
 * absent and .uninit retains its NOLOAD value. No cache to flush on the M4.
 */
void tiku_mem_arch_init(void) {
    const uint32_t *mirror = (const uint32_t *)&__tiku_nvm_mram_start;
    if (mirror[0] == TIKU_NVM_MAGIC) {
        size_t n = uninit_bytes();
        if (n > (TIKU_NVM_MRAM_BYTES - 4U)) {
            n = TIKU_NVM_MRAM_BYTES - 4U;
        }
        memcpy(&__uninit_start, (const uint8_t *)&mirror[1], n);
    }
}

/** @brief Zero-fill a buffer via a volatile pointer (defeats optimization). */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    volatile uint8_t *p = buf;
    while (len--) {
        *p++ = 0u;
    }
}

/** @brief Copy from the .uninit NVM region (memory-mapped SRAM) into a buffer. */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len) {
    memcpy(dst, src, len);
}

/** @brief Write into the .uninit SRAM working copy (committed at nvm_flush). */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    memcpy(dst, src, len);
}

/**
 * @brief Snapshot .uninit into the reserved MRAM page for power-cycle durability
 *
 * Composes a TIKU_NVM_MAGIC header + the entire .uninit image into the TCM
 * staging buffer g_nvm_snap, pads to a 16-byte boundary, and programs the
 * result into the reserved MRAM page via the Apollo4 bootrom nv_program_main2
 * helper. The destination is passed as a word offset from the MRAM origin
 * (0x0), so dst_word_offset = page_addr >> 2. Interrupts are masked across the
 * program call (the helper executes from ROM, so MRAM stays fetchable). MRAM is
 * direct-write; no erase and -- on the cacheless M4 -- no cache maintenance.
 */
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

    /* Destination word offset from the MRAM origin (0x0). */
    dst_word_off =
        ((uint32_t)(uintptr_t)&__tiku_nvm_mram_start - AMBIQ_MRAM_BASE) >> 2;

    /* MRAM program is uninterruptible (an ISR fetch could fault mid-program). */
    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    __asm__ volatile ("cpsid i" ::: "memory");
    (void)NV_PROGRAM_MAIN2(AMBIQ_MRAM_PROGRAM_KEY, AMBIQ_MRAM_OP_PROGRAM,
                           (uint32_t)(uintptr_t)g_nvm_snap, dst_word_off,
                           (uint32_t)(prog_bytes / 4U));
    __asm__ volatile ("msr primask, %0" : : "r"(primask) : "memory");

    /* The bootrom wrote MRAM out-of-band; drop any cached copies of the page so
     * same-session reads of the mirror see the new data. With the D-cache
     * enabled (soc_init) this is required for coherency; the Apollo4 CACHECTRL
     * has no by-range op so it invalidates the whole cache. */
    tiku_cpu_dcache_invalidate((const void *)&__tiku_nvm_mram_start, prog_bytes);
}
