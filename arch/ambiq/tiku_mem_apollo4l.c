/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_apollo4l.c - Apollo4 Lite memory architecture + MRAM-backed NVM
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
 *   - the Cortex-M4 has NO SCB L1 D-cache, so the TCM staging buffer needs no
 *     clean before the bootrom reads it (unlike the M55/SSRAM path on 510).
 *     The Apollo4 CACHECTRL does, however, cache MRAM reads, so the mirror
 *     page is invalidated after a real program -- and the flush dirty-check
 *     relies on that invalidate to keep its compare coherent.
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
#include "tiku_mpu_arch.h"  /* arch NVM window around the mirror restore */
#include "tiku_cpu_common.h"  /* tiku_cpu_ambiq_delay_us (bench DWT calibration) */
#include "tiku_mram_bench.h"  /* tiku_mem_nvm_bench_row_t */
#include <hal/tiku_cpu.h>   /* tiku_cpu_dcache_invalidate (D-cache coherency) */

/* Live .uninit working copy + the reserved MRAM mirror page (apollo4l.ld). */
extern uint8_t  __uninit_start;
extern uint8_t  __uninit_end;
extern uint32_t __tiku_nvm_mram_start[]; /* base of the mirror page (linker
                                            * symbol: incomplete array so the
                                            * compiler cannot assume a size) */

/**
 * @defgroup MEM_ARCH_CONSTS Apollo4 Lite MRAM driver constants
 * @brief Fixed addresses, keys, and sizes for the bootrom MRAM programmer.
 * @{
 */
#define AMBIQ_MRAM_BASE         0x00000000UL  /* AM_HAL_MRAM_ADDR (word-offset origin) */
#define AMBIQ_MRAM_PROGRAM_KEY  0x12344321UL  /* AM_HAL_MRAM_PROGRAM_KEY */
#define AMBIQ_MRAM_OP_PROGRAM   1U            /* program main array (fill = 0) */
#include "kernel/memory/tiku_nvm_mirror.h"
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
    const uint32_t *mirror = (const uint32_t *)__tiku_nvm_mram_start;
    size_t n = uninit_bytes();
    uint16_t mpu_saved;

    /* The restore memcpys write .uninit.  At FIRST boot the MPU is not
     * armed yet (mpu_init runs after arch_init), but tiku_mem_init() is
     * legitimately re-callable (tests, recovery paths) -- and by then
     * region 0 is genuinely read-only, so an unbracketed restore is a
     * MemManage fault.  Found exactly that way: the enforcement flip
     * turned the memory-edge re-init test into a reset loop.  Bracket
     * with the ARCH window (no flush side-effects; nest-safe). */
    mpu_saved = tiku_mpu_arch_unlock_nvm();

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

    tiku_mpu_arch_lock_nvm(mpu_saved);
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
     * matches the mirror byte-for-byte -- i.e. nothing in .uninit changed
     * since the last commit.  This is the same optimization the Apollo510
     * flush already carries (arch/ambiq/tiku_mem_arch.c); it was never
     * ported here, so this part re-programmed the full 64 KB mirror on EVERY
     * tiku_mpu_lock_nvm().  The dominant caller is the TCP per-packet NVM
     * relock, whose RX/TX buffers live in .bss, NOT .uninit -- so with this
     * check those relocks touch MRAM zero times: no wear, no program latency,
     * no long IRQ-off program window.  Persist-cell writes DO change .uninit,
     * so they still commit.
     *
     * Coherency: the only out-of-band writer of the mirror page is the
     * bootrom program below, and every such program is followed by the
     * CACHECTRL invalidate, so this compare always sees current mirror data
     * (cold after reset, freshly invalidated after a program).  The compose
     * above and the IRQ-off window below are kept on EVERY call so any
     * ordering/timing side-effect a per-packet relock relies on is unchanged;
     * only the program itself and its mirror invalidate are conditional. */
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

    /* Destination word offset from the MRAM origin (0x0). */
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

    /* Only a real program disturbs the mirror; drop the CACHECTRL copies of
     * the page so same-session reads see the new data.  The Cortex-M4 has no
     * SCB L1 D-cache, but the Apollo4 CACHECTRL caches MRAM reads and has no
     * by-range op, so this invalidates the whole cache.  Skipped when nothing
     * changed, so an idle relock costs nothing -- and this is exactly the
     * invariant the dirty-check compare above relies on for coherency. */
    if (changed) {
        tiku_cpu_dcache_invalidate((const void *)__tiku_nvm_mram_start, prog_bytes);
        g_nvm_flush_programs++;
    }
}

/*---------------------------------------------------------------------------*/
/* MRAM program-timing benchmark (mrambench command)                         */
/*---------------------------------------------------------------------------*/

/* Raw DWT cycle counter (Cortex-M4), addressed directly to match the raw
 * SysTick idiom in tiku_cpu_common.c and avoid a core_cm4.h dependency.
 * The 24-bit SysTick used for delays wraps in ~175 us at 96 MHz -- too short
 * to single-shot a possibly-millisecond MRAM program -- so the bench uses the
 * 32-bit DWT cycle counter (no wrap for ~44 s) and calibrates its rate. */
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
    /* The live image sits in [0, 4 + uninit) at the bottom; refuse if it would
     * reach into the upper-half scratch window (keeps the bench non-destructive
     * to durable state and power-cut-safe -- the magic+data stay intact). */
    if ((4U + uninit_bytes()) > bench_off) { return 0U; }

    /* Enable + zero the DWT cycle counter. */
    TIKU_SCB_DEMCR |= (1UL << 24);     /* TRCENA */
    TIKU_DWT_CYCCNT = 0U;
    TIKU_DWT_CTRL  |= 1UL;             /* CYCCNTENA */

    /* Calibrate DWT ticks/second against the trusted SysTick us-delay, so the
     * us conversion is right regardless of the part's DWT:core ratio (1x on the
     * M4, 2x on the M55).  Raw cycles below are rate-independent for shape. */
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

        /* Varied source pattern (bit transitions) so we don't measure an
         * all-identical fast path. Clobbers the staging buffer; the closing
         * lock_nvm flush recomposes it. */
        for (w = 0U; w < words; w++) {
            g_nvm_snap[w] = 0xA5A50000UL ^ (uint32_t)(w * 2654435761UL);
        }

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

    /* Drop CACHECTRL copies of the clobbered scratch; the live image (bottom
     * half) was never touched, so no flush/restore of durable state is needed. */
    tiku_cpu_dcache_invalidate((const void *)(mirror + bench_off),
                               TIKU_NVM_MRAM_BYTES - bench_off);
    return count;
}
