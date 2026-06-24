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
#include <hal/tiku_cpu.h>   /* tiku_cpu_dcache_{clean,invalidate} (D-cache coherency) */

/* Live .uninit working copy + the reserved MRAM mirror page (apollo510.ld). */
extern uint8_t  __uninit_start;
extern uint8_t  __uninit_end;
extern uint32_t __tiku_nvm_mram_start;   /* base address of the mirror page */

/**
 * @defgroup MEM_ARCH_CONSTS Apollo510 MRAM driver constants
 * @brief Fixed addresses, keys, and sizes for the bootrom MRAM programmer.
 * @{
 */
#define AMBIQ_MRAM_BASE         0x00400000UL  /* AM_HAL_MRAM_ADDR (word-offset origin) */
#define AMBIQ_MRAM_PROGRAM_KEY  0x12344321UL  /* AM_HAL_MRAM_PROGRAM_KEY */
#define AMBIQ_MRAM_OP_PROGRAM   1U            /* AM_HAL_MRAM_PROGRAM */
#define TIKU_NVM_MAGIC          0x4E564D54U   /* 'NVMT' little-endian */
#define TIKU_NVM_MRAM_BYTES     0x10000U      /* 64 KB; MUST match __tiku_nvm_mram_size */
/** @} */

/**
 * @brief On-chip bootrom MRAM programmer function type
 *
 * Fixed ROM entry at 0x0200ff20 (Thumb bit set) from the AmbiqSuite
 * bootrom helper table (g_am_hal_bootrom_helper.nv_program_main2).
 * Signature: nv_program_main2(key, op, src_addr, dst_word_offset, num_words).
 * MRAM is direct-write (no erase required, unlike NOR flash).
 */
typedef int (*nv_program_main2_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#define NV_PROGRAM_MAIN2  ((nv_program_main2_t)(0x0200ff20UL + 1UL))

/**
 * @brief Page-sized SSRAM staging buffer for MRAM programming
 *
 * Placed in the 3 MB shared SSRAM (.ssram section) and 16-byte aligned
 * as required by the bootrom MRAM programmer. Layout: word[0] = magic,
 * words[1..] = .uninit image, 0xFF padding to a 16-byte boundary.
 */
static uint32_t g_nvm_snap[TIKU_NVM_MRAM_BYTES / 4U]
    __attribute__((section(".ssram"), aligned(16)));

/**
 * @brief Return the size of the .uninit region in bytes
 *
 * Computed from the linker symbols __uninit_start and __uninit_end.
 *
 * @return Number of bytes in the .uninit section
 */
static size_t uninit_bytes(void) {
    return (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);
}

/**
 * @brief Restore .uninit state from the MRAM mirror on boot
 *
 * Checks word[0] of the reserved MRAM page for TIKU_NVM_MAGIC. If the
 * magic matches, copies the stored .uninit image back into RAM, making
 * power-cycle-durable state visible to subsequent subsystem init code.
 * On a fresh chip the magic is absent and .uninit retains its NOLOAD
 * (uninitialized) value; each subsystem's "no magic -> init fresh"
 * guard handles that case. Cache is empty or off at this point, so the
 * MRAM read reflects current MRAM contents.
 */
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

/**
 * @brief Zero-fill a buffer using a volatile pointer to defeat optimization
 *
 * Uses a volatile write loop so the compiler cannot elide the stores when
 * the buffer is not subsequently read (e.g. after zeroing a credential
 * buffer before releasing it).
 *
 * @param buf  Buffer to wipe
 * @param len  Number of bytes to zero
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    volatile uint8_t *p = buf;
    while (len--) {
        *p++ = 0u;
    }
}

/**
 * @brief Copy bytes from the .uninit NVM region into an SRAM buffer
 *
 * The .uninit stand-in is memory-mapped SRAM (DTCM), so this is a plain
 * memcpy with no wait states.
 *
 * @param dst  Destination SRAM buffer
 * @param src  Source address inside the .uninit region
 * @param len  Number of bytes to copy
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len) {
    memcpy(dst, src, len);   /* .uninit NVM stand-in is memory-mapped SRAM */
}

/**
 * @brief Write bytes into the .uninit SRAM working copy
 *
 * Stores data into the in-RAM working copy only. The durable MRAM commit
 * happens when the matching tiku_mpu_lock_nvm() relock calls
 * tiku_mem_arch_nvm_flush(), so direct stores into .persistent inside an
 * unlock window are also captured by that flush.
 *
 * @param dst  Destination address inside the .uninit region
 * @param src  Source SRAM buffer
 * @param len  Number of bytes to copy
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    /* SRAM working copy; the MRAM commit happens at the matching
     * tiku_mpu_lock_nvm() relock via tiku_mem_arch_nvm_flush(), so direct
     * stores into .persistent inside the unlock window are captured too. */
    memcpy(dst, src, len);
}

/**
 * @brief Snapshot .uninit into the reserved MRAM page for power-cycle durability
 *
 * Composes a TIKU_NVM_MAGIC header followed by the entire .uninit image
 * into the SSRAM staging buffer g_nvm_snap, pads to a 16-byte boundary,
 * and programs the result into the reserved MRAM page via the on-chip
 * bootrom nv_program_main2 helper. Steps:
 *
 *   1. Fill g_nvm_snap: magic word + .uninit image + 0xFF pad.
 *   2. Clean the SSRAM D-cache lines covering g_nvm_snap so the bootrom
 *      DMA reads coherent data.
 *   3. Compute the MRAM word-offset of the mirror page from the linker
 *      symbol __tiku_nvm_mram_start.
 *   4. Disable interrupts around the MRAM program call: an ISR fetch
 *      mid-program could fault because MRAM is temporarily busy.
 *   5. Invalidate the D-cache lines covering the freshly-programmed page
 *      so same-session reads of the mirror see the new data.
 *
 * MRAM is direct-write (no erase unlike NOR flash). The bootrom helper
 * operates in units of 32-bit words and requires 16-byte-aligned source.
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

    /* Write the snapshot back to SSRAM so the MRAM programmer reads it there. */
    tiku_cpu_dcache_clean((const void *)g_nvm_snap, prog_bytes);

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
    tiku_cpu_dcache_invalidate((const void *)&__tiku_nvm_mram_start, prog_bytes);
}
