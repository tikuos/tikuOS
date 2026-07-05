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
#include "tiku_cpu_common.h"  /* tiku_cpu_ambiq_delay_us (bench DWT calibration) */
#include "tiku_mram_bench.h"  /* tiku_mem_nvm_bench_row_t */
#include <hal/tiku_cpu.h>   /* tiku_cpu_dcache_{clean,invalidate} (D-cache coherency) */

/* Live .uninit working copy + the reserved MRAM mirror page (apollo510.ld). */
extern uint8_t  __uninit_start;
extern uint8_t  __uninit_end;
extern uint32_t __tiku_nvm_mram_start[]; /* base of the mirror page (linker
                                            * symbol: incomplete array so the
                                            * compiler cannot assume a size) */

/**
 * @defgroup MEM_ARCH_CONSTS Apollo510 MRAM driver constants
 * @brief Fixed addresses, keys, and sizes for the bootrom MRAM programmer.
 * @{
 */
#define AMBIQ_MRAM_BASE         0x00400000UL  /* AM_HAL_MRAM_ADDR (word-offset origin) */
#define AMBIQ_MRAM_PROGRAM_KEY  0x12344321UL  /* AM_HAL_MRAM_PROGRAM_KEY */
#define AMBIQ_MRAM_OP_PROGRAM   1U            /* AM_HAL_MRAM_PROGRAM */
#include "kernel/memory/tiku_nvm_mirror.h"
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

/* Count of real mirror programs the flush has performed (i.e. flushes where
 * the dirty-check found a change).  Observability + the mrambench dirty-check
 * self-test: an idle flush must leave this unchanged. */
static uint32_t g_nvm_flush_programs;

uint32_t tiku_mem_arch_nvm_program_count(void) { return g_nvm_flush_programs; }

/** Boot-time mirror-restore outcome (see tiku_nvm_restore_t). */
static tiku_nvm_restore_t g_nvm_restore;

tiku_nvm_restore_t tiku_mem_arch_nvm_restore_status(void)
{
    return g_nvm_restore;
}

/** Base of the NVM mirror (header + image), for tests/diagnostics. */
const uint8_t *tiku_mem_arch_nvm_mirror(void)
{
    return (const uint8_t *)__tiku_nvm_mram_start;
}


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
    /* Restore .uninit from the MRAM mirror. On a fresh boot the cache is
     * empty (or off) here, so reading the page sees current MRAM contents.
     * V2 mirrors are CRC-validated (a torn flush is detected and NOT
     * restored); V1 mirrors are accepted once for seamless upgrade. */
    const uint32_t *mirror = (const uint32_t *)__tiku_nvm_mram_start;
    size_t n = uninit_bytes();

    if (mirror[TIKU_NVM_MIRROR_W_MAGIC] == TIKU_NVM_MIRROR_MAGIC_V2) {
        size_t len = (size_t)mirror[TIKU_NVM_MIRROR_W_LEN];
        const uint8_t *img =
            (const uint8_t *)__tiku_nvm_mram_start + TIKU_NVM_MIRROR_HDR_BYTES;
        if (len <= (TIKU_NVM_MRAM_BYTES - TIKU_NVM_MIRROR_HDR_BYTES) &&
            tiku_nvm_crc32(img, len) == mirror[TIKU_NVM_MIRROR_W_CRC]) {
            if (n > len) {
                n = len;
            }
            memcpy(&__uninit_start, img, n);
            g_nvm_restore = TIKU_NVM_RESTORE_V2_OK;
        } else {
            /* Torn program (power cut mid-flush) or rot: the magic word
             * survived but the image does not check out.  DO NOT restore
             * — .uninit keeps its NOLOAD value and every subsystem's
             * "gate invalid -> prime default" path runs.  Crash-
             * consistent by construction, never silently corrupt. */
            g_nvm_restore = TIKU_NVM_RESTORE_CRC_FAIL;
        }
    } else if (mirror[0] == TIKU_NVM_MIRROR_MAGIC_V1) {
        /* Legacy pre-CRC mirror: accept it once (best effort, exactly
         * the old behavior) so an upgrade keeps boot_count/RTC/aliases;
         * the first flush after this boot rewrites the mirror as V2. */
        if (n > (TIKU_NVM_MRAM_BYTES - 4U)) {
            n = TIKU_NVM_MRAM_BYTES - 4U;
        }
        memcpy(&__uninit_start, (const uint8_t *)&mirror[1], n);
        g_nvm_restore = TIKU_NVM_RESTORE_V1;
    } else {
        g_nvm_restore = TIKU_NVM_RESTORE_VIRGIN;
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
    int      changed;

    if (n > (TIKU_NVM_MRAM_BYTES - TIKU_NVM_MIRROR_HDR_BYTES)) {
        n = TIKU_NVM_MRAM_BYTES - TIKU_NVM_MIRROR_HDR_BYTES;
    }

    /* Compose the IMAGE first (header words filled only if we program:
     * the CRC is the expensive part and a clean relock must stay free). */
    memcpy((uint8_t *)&g_nvm_snap[4], &__uninit_start, n);
    snap_bytes = TIKU_NVM_MIRROR_HDR_BYTES + n;
    prog_bytes = (snap_bytes + 15U) & ~((size_t)15U);
    if (prog_bytes > TIKU_NVM_MRAM_BYTES) {
        prog_bytes = TIKU_NVM_MRAM_BYTES;
    }
    if (prog_bytes > snap_bytes) {
        memset((uint8_t *)g_nvm_snap + snap_bytes, 0xFFu, prog_bytes - snap_bytes);
    }

    /* Dirty check: skip the MRAM program when the composed image already
     * matches the mirror byte-for-byte -- i.e. nothing in .uninit changed since
     * the last commit.  High-frequency callers (notably the TCP per-packet NVM
     * relock, whose RX/TX buffers are in .bss, NOT .uninit) thus stop
     * re-programming the mirror on every call: no MRAM wear, no program
     * latency.  Persist-cell writes DO change .uninit, so they still commit.
     * The mirror stays cache-coherent via the post-program invalidate below
     * (and is cold-cache-fresh after reset), so the compare reads current data.
     * The dcache-clean and the brief IRQ-off window below are kept on EVERY
     * call -- only the program (and its mirror invalidate) is conditional -- so
     * any ordering/timing side-effect a per-packet relock relies on is intact. */
    {
        const uint32_t *mirror = (const uint32_t *)__tiku_nvm_mram_start;
        changed = (mirror[TIKU_NVM_MIRROR_W_MAGIC] != TIKU_NVM_MIRROR_MAGIC_V2 ||
                   mirror[TIKU_NVM_MIRROR_W_LEN]   != (uint32_t)n ||
                   memcmp((const uint8_t *)&g_nvm_snap[4],
                          (const uint8_t *)__tiku_nvm_mram_start +
                              TIKU_NVM_MIRROR_HDR_BYTES,
                          prog_bytes - TIKU_NVM_MIRROR_HDR_BYTES) != 0);
    }

    /* Fill the header only when programming: magic, CRC over the image,
     * image length, reserved-erased.  On an unchanged image the mirror's
     * existing header is necessarily the header of THIS image, so the
     * skip needs no CRC work at all. */
    if (changed) {
        g_nvm_snap[TIKU_NVM_MIRROR_W_MAGIC] = TIKU_NVM_MIRROR_MAGIC_V2;
        g_nvm_snap[TIKU_NVM_MIRROR_W_CRC]   =
            tiku_nvm_crc32((const uint8_t *)&g_nvm_snap[4], n);
        g_nvm_snap[TIKU_NVM_MIRROR_W_LEN]   = (uint32_t)n;
        g_nvm_snap[TIKU_NVM_MIRROR_W_RSVD]  = 0xFFFFFFFFu;
    }

    /* Write the snapshot back to SSRAM so the MRAM programmer reads it there. */
    tiku_cpu_dcache_clean((const void *)g_nvm_snap, prog_bytes);

    dst_word_off =
        ((uint32_t)(uintptr_t)__tiku_nvm_mram_start - AMBIQ_MRAM_BASE) >> 2;

    /* MRAM program is uninterruptible (an ISR fetch could fault mid-program). */
    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    __asm__ volatile ("cpsid i" ::: "memory");
    if (changed) {
        (void)NV_PROGRAM_MAIN2(AMBIQ_MRAM_PROGRAM_KEY, AMBIQ_MRAM_OP_PROGRAM,
                               (uint32_t)(uintptr_t)g_nvm_snap, dst_word_off,
                               (uint32_t)(prog_bytes / 4U));
    }
    __asm__ volatile ("msr primask, %0" : : "r"(primask) : "memory");

    /* Drop any cached copies of the freshly-programmed page so same-session
     * reads of the mirror see the new data. */
    if (changed) {
        tiku_cpu_dcache_invalidate((const void *)__tiku_nvm_mram_start, prog_bytes);
        g_nvm_flush_programs++;
    }
}

/*---------------------------------------------------------------------------*/
/* MRAM program-timing benchmark (mrambench command)                         */
/*---------------------------------------------------------------------------*/

/* Raw DWT cycle counter (Cortex-M55), addressed directly to match the raw
 * SysTick idiom in tiku_cpu_common.c.  The 24-bit SysTick used for delays
 * wraps too fast (~87 us at 192 MHz) to single-shot a possibly-millisecond
 * MRAM program, so the bench uses the 32-bit DWT counter and calibrates its
 * rate (the M55 DWT ticks at 2x the core, which the calibration absorbs). */
#define TIKU_DWT_CTRL    (*(volatile uint32_t *)0xE0001000UL)
#define TIKU_DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004UL)
#define TIKU_SCB_DEMCR   (*(volatile uint32_t *)0xE000EDFCUL)

uint8_t tiku_mem_arch_nvm_bench(tiku_mem_nvm_bench_row_t *rows, uint8_t max,
                                unsigned long *dwt_hz_out)
{
    static const uint16_t sizes[] = { 16U, 256U, 4096U, 32768U };
    const uint8_t   nsizes    = (uint8_t)(sizeof(sizes) / sizeof(sizes[0]));
    const uintptr_t mirror    = (uintptr_t)__tiku_nvm_mram_start;
    const size_t    bench_off = TIKU_NVM_MRAM_BYTES / 2U;  /* upper half = scratch */
    uint32_t primask, c0, c1;
    uint8_t  i, r, count = 0U;

    if (dwt_hz_out) { *dwt_hz_out = 0UL; }
    if (rows == NULL || max == 0U) { return 0U; }
    if ((4U + uninit_bytes()) > bench_off) { return 0U; }   /* image too large */

    TIKU_SCB_DEMCR |= (1UL << 24);     /* TRCENA */
    TIKU_DWT_CYCCNT = 0U;
    TIKU_DWT_CTRL  |= 1UL;             /* CYCCNTENA */

    c0 = TIKU_DWT_CYCCNT;
    tiku_cpu_ambiq_delay_us(5000u);    /* 5 ms */
    c1 = TIKU_DWT_CYCCNT;
    if (dwt_hz_out) { *dwt_hz_out = (unsigned long)(c1 - c0) * 200UL; }

    for (i = 0U; i < nsizes && count < max; i++) {
        uint32_t words = (uint32_t)sizes[i] / 4U;
        uint32_t best  = 0xFFFFFFFFUL;
        uint32_t dst_word_off =
            (uint32_t)(((mirror + bench_off) - AMBIQ_MRAM_BASE) >> 2);
        uint32_t w;

        for (w = 0U; w < words; w++) {
            g_nvm_snap[w] = 0xA5A50000UL ^ (uint32_t)(w * 2654435761UL);
        }
        /* g_nvm_snap is cached SSRAM on the M55: clean the source lines so the
         * bootrom reads the just-written pattern, not a stale cache image. */
        tiku_cpu_dcache_clean((const void *)g_nvm_snap, (size_t)sizes[i]);

        for (r = 0U; r < 4U; r++) {    /* best-of-4 discards outliers */
            __asm__ volatile ("mrs %0, primask" : "=r"(primask));
            __asm__ volatile ("cpsid i" ::: "memory");
            c0 = TIKU_DWT_CYCCNT;
            (void)NV_PROGRAM_MAIN2(AMBIQ_MRAM_PROGRAM_KEY, AMBIQ_MRAM_OP_PROGRAM,
                                   (uint32_t)(uintptr_t)g_nvm_snap,
                                   dst_word_off, words);
            c1 = TIKU_DWT_CYCCNT;
            __asm__ volatile ("msr primask, %0" : : "r"(primask) : "memory");
            if ((c1 - c0) < best) { best = c1 - c0; }
        }
        rows[count].bytes  = sizes[i];
        rows[count].cycles = best;
        count++;
    }

    /* Drop cached copies of the clobbered scratch; the live image (bottom
     * half) was never touched, so no flush/restore of durable state is needed. */
    tiku_cpu_dcache_invalidate((const void *)(mirror + bench_off),
                               TIKU_NVM_MRAM_BYTES - bench_off);
    return count;
}
