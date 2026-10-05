/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - Apollo510 memory architecture and MRAM-backed NVM.
 *
 * Persistent state lives in .uninit and is mirrored to a reserved MRAM page via
 * the on-chip bootrom, restored at boot when the mirror checks out.  MRAM takes
 * direct writes with no erase; the M55's L1 D-cache is maintained around each.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include "tiku_mem_arch.h"
#include "tiku_mpu_arch.h"  /* arch NVM window around the mirror restore */
#include "tiku_cpu_common.h"  /* tiku_cpu_ambiq_delay_us (bench) */
#include "tiku_mram_bench.h"  /* tiku_mem_nvm_bench_row_t */
#include <hal/tiku_cpu.h>   /* tiku_cpu_dcache_clean / _invalidate */

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
#define AMBIQ_MRAM_BASE         0x00400000UL  /* AM_HAL_MRAM_ADDR */
#define AMBIQ_MRAM_PROGRAM_KEY  0x12344321UL  /* AM_HAL_MRAM_PROGRAM_KEY */
#define AMBIQ_MRAM_OP_PROGRAM   1U            /* AM_HAL_MRAM_PROGRAM */
#include "kernel/memory/tiku_nvm_mirror.h"
#define TIKU_NVM_MRAM_BYTES     0x4000U       /* 16 KB: __tiku_nvm_mram_size */
/** @} */

/**
 * @brief On-chip bootrom MRAM programmer function type
 *
 * Fixed ROM entry at 0x0200ff20 (Thumb bit set) from the AmbiqSuite bootrom
 * helper table.  Signature: nv_program_main2(key, op, src_addr,
 * dst_word_offset, num_words).  MRAM needs no erase before a program.
 */
typedef int (*nv_program_main2_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#define NV_PROGRAM_MAIN2  ((nv_program_main2_t)(0x0200ff20UL + 1UL))

/**
 * @brief Page-sized SSRAM staging buffer for MRAM programming
 *
 * Placed in the 3 MB shared SSRAM (.ssram section) and 16-byte aligned as the
 * bootrom MRAM programmer requires.  Layout: words 0-3 = mirror header, then
 * the .uninit image, 0xFF padding to a 16-byte boundary.
 */
static uint32_t g_nvm_snap[TIKU_NVM_MRAM_BYTES / 4U]
    __attribute__((section(".ssram"), aligned(16)));

/* Successful mirror programs: flushes whose dirty check found a change.
 * Read by the mrambench self-test, which expects an idle flush to leave it
 * unchanged. */
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
 * Copies the mirror's .uninit image back into RAM when tiku_nvm_mirror_image()
 * accepts it, or a legacy V1 image.  With no image that checks out, .uninit is
 * zeroed and each cell primes its default.
 *
 * @note At first boot the cache is empty or off, so the MRAM read reflects
 *       current MRAM contents.
 */
void tiku_mem_arch_init(void) {
    /* Restore .uninit from the MRAM mirror.  On a fresh boot the cache is
     * empty (or off) here, so reading the page sees current MRAM contents.
     * A V2 mirror is CRC-checked, so a torn flush is not restored; a V1
     * mirror is restored as is. */
    const uint32_t *mirror = (const uint32_t *)__tiku_nvm_mram_start;
    const uint8_t *img;
    size_t n = uninit_bytes();
    size_t len;
    uint16_t mpu_saved;

    /* The restore memcpys write .uninit.  At first boot the MPU is not
     * armed here (mpu_init runs after arch_init), but the memory tests call
     * tiku_mem_init() again, when region 0 is read-only and an unbracketed
     * restore is a MemManage fault.  The arch window opens it with no flush
     * side effects, and it nests. */
    mpu_saved = tiku_mpu_arch_unlock_nvm();

    img = tiku_nvm_mirror_image(mirror, TIKU_NVM_MRAM_BYTES, &len);
    if (img != NULL) {
        memcpy(&__uninit_start, img, (n < len) ? n : len);
        if (n > len) {          /* this image grew: its new cells start blank */
            memset(&__uninit_start + len, 0, n - len);
        }
        g_nvm_restore = TIKU_NVM_RESTORE_V2_OK;
    } else if (mirror[0] == TIKU_NVM_MIRROR_MAGIC_V1) {
        /* Legacy V1 mirror (no CRC): restored as is, so state an older
         * image wrote (boot_count, RTC, aliases) survives; the first flush
         * after this boot rewrites the mirror as V2. */
        if (n > (TIKU_NVM_MRAM_BYTES - 4U)) {
            n = TIKU_NVM_MRAM_BYTES - 4U;
        }
        memcpy(&__uninit_start, (const uint8_t *)&mirror[1], n);
        g_nvm_restore = TIKU_NVM_RESTORE_V1;
    } else {
        /* A fresh part, or a flush torn by a power cut (the magic survived,
         * the image does not check out).  A reset does not clear .uninit, so
         * zero it: every "gate invalid -> prime default" path then runs, and
         * nothing from before the reset passes for a restored value. */
        memset(&__uninit_start, 0, n);
        g_nvm_restore =
            (mirror[TIKU_NVM_MIRROR_W_MAGIC] == TIKU_NVM_MIRROR_MAGIC_V2)
                ? TIKU_NVM_RESTORE_CRC_FAIL : TIKU_NVM_RESTORE_VIRGIN;
    }

    tiku_mpu_arch_lock_nvm(mpu_saved);
}

/** @brief The mirror's image when it checks out (see tiku_mem_hal.h). */
const uint8_t *tiku_mem_arch_durable(size_t *len)
{
    return tiku_nvm_mirror_image(__tiku_nvm_mram_start, TIKU_NVM_MRAM_BYTES,
                                 len);
}

/** @brief The .uninit window durable variables live in (see tiku_mem_hal.h). */
uint8_t *tiku_mem_arch_durable_live(size_t *len)
{
    *len = (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);
    return (uint8_t *)&__uninit_start;
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
 * The .uninit working copy is memory-mapped SRAM (DTCM), so this is a plain
 * memcpy with no wait states.
 *
 * @param dst  Destination SRAM buffer
 * @param src  Source address inside the .uninit region
 * @param len  Number of bytes to copy
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len) {
    memcpy(dst, src, len);   /* .uninit working copy is memory-mapped SRAM */
}

/**
 * @brief Write bytes into the .uninit SRAM working copy
 *
 * Stores into the in-RAM working copy only.  The durable MRAM commit happens
 * when the matching tiku_mpu_lock_nvm() calls tiku_mem_arch_nvm_flush_status(),
 * so direct stores into .persistent inside a window are captured too.
 *
 * @param dst  Destination address inside the .uninit region
 * @param src  Source SRAM buffer
 * @param len  Number of bytes to copy
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    /* SRAM working copy; the MRAM commit happens at the matching
     * tiku_mpu_lock_nvm() via tiku_mem_arch_nvm_flush_status(), so direct
     * stores into .persistent inside the unlock window are captured too. */
    memcpy(dst, src, len);
    /* TIKU_RETAINED data (.retained) lives in SSRAM, which the M55 D-cache
     * holds write-back: a reset discards dirty lines and the write with
     * them, so the bytes are cleaned to memory here.  TIKU_DURABLE data
     * (.persistent) is in uncached DTCM and its mirror flush cleans its own
     * staging, so the by-range clean costs little there. */
    tiku_cpu_dcache_clean(dst, len);
}

/*
 * Commit .uninit to the reserved MRAM page for power-cycle durability, via the
 * on-chip bootrom nv_program_main2 helper.  Steps:
 *
 *   1. Compose the .uninit image after the header slot in g_nvm_snap, padded
 *      with 0xFF to a 16-byte boundary.
 *   2. Compare it with the mirror; when they match, skip the program.
 *   3. Otherwise fill the header (magic, CRC, length, reserved).
 *   4. Clean the SSRAM D-cache lines covering g_nvm_snap so the bootrom
 *      reads coherent data.
 *   5. Mask interrupts around the program call: an ISR fetch mid-program
 *      could fault because MRAM is busy.
 *   6. Invalidate the D-cache lines covering the programmed page so
 *      same-session reads of the mirror see the new data.
 *
 * The bootrom helper works in 32-bit words and needs a 16-byte-aligned
 * source.
 */
int tiku_mem_arch_nvm_flush_status(void) {
    size_t   n = uninit_bytes();
    size_t   snap_bytes, prog_bytes;
    uint32_t dst_word_off, primask;
    int      changed;
    int      rc = 0;

    if (n > (TIKU_NVM_MRAM_BYTES - TIKU_NVM_MIRROR_HDR_BYTES)) {
        return -1;
    }

    /* Compose the image first; the header words are filled only when a
     * program follows, so a relock with nothing changed computes no CRC. */
    memcpy((uint8_t *)&g_nvm_snap[4], &__uninit_start, n);
    snap_bytes = TIKU_NVM_MIRROR_HDR_BYTES + n;
    prog_bytes = (snap_bytes + 15U) & ~((size_t)15U);
    if (prog_bytes > TIKU_NVM_MRAM_BYTES) {
        return -1;
    }
    if (prog_bytes > snap_bytes) {
        memset((uint8_t *)g_nvm_snap + snap_bytes, 0xFFu, prog_bytes - snap_bytes);
    }

    /* Dirty check: skip the MRAM program when the composed image already
     * matches the mirror byte for byte, i.e. nothing in .uninit changed since
     * the last commit.  A relock that changes nothing in .uninit, such as the
     * TCP per-packet NVM relock (its RX/TX buffers are in .bss), then costs
     * no MRAM program.  The post-program invalidate below keeps cached
     * mirror lines current (and the cache is cold after reset), so the
     * compare reads current data.  The D-cache clean and the interrupt-masked
     * window below run on every call; only the program and its invalidate
     * are conditional. */
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
     * header is already the header of this image. */
    if (changed) {
        g_nvm_snap[TIKU_NVM_MIRROR_W_MAGIC] = TIKU_NVM_MIRROR_MAGIC_V2;
        g_nvm_snap[TIKU_NVM_MIRROR_W_CRC]   =
            tiku_nvm_crc32((const uint8_t *)&g_nvm_snap[4], n);
        g_nvm_snap[TIKU_NVM_MIRROR_W_LEN]   = (uint32_t)n;
        g_nvm_snap[TIKU_NVM_MIRROR_W_RSVD]  = 0xFFFFFFFFu;
    }

    /* Clean the snapshot to SSRAM, where the MRAM programmer reads it. */
    tiku_cpu_dcache_clean((const void *)g_nvm_snap, prog_bytes);

    dst_word_off =
        ((uint32_t)(uintptr_t)__tiku_nvm_mram_start - AMBIQ_MRAM_BASE) >> 2;

    /* Interrupts masked: an ISR fetch could fault mid-program. */
    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    __asm__ volatile ("cpsid i" ::: "memory");
    if (changed) {
        rc = NV_PROGRAM_MAIN2(AMBIQ_MRAM_PROGRAM_KEY, AMBIQ_MRAM_OP_PROGRAM,
                               (uint32_t)(uintptr_t)g_nvm_snap, dst_word_off,
                               (uint32_t)(prog_bytes / 4U));
    }
    __asm__ volatile ("msr primask, %0" : : "r"(primask) : "memory");

    /* Drop any cached copies of the freshly-programmed page so same-session
     * reads of the mirror see the new data. */
    if (changed) {
        tiku_cpu_dcache_invalidate((const void *)__tiku_nvm_mram_start, prog_bytes);
        if (rc == 0) { g_nvm_flush_programs++; }
    }
    return rc == 0 ? 0 : -1;
}

/** @brief tiku_mem_arch_nvm_flush_status() with the result discarded. */
void tiku_mem_arch_nvm_flush(void)
{
    (void)tiku_mem_arch_nvm_flush_status();
}

/*---------------------------------------------------------------------------*/
/* MRAM PROGRAM-TIMING BENCHMARK                                             */
/*---------------------------------------------------------------------------*/

/* Raw DWT cycle counter (Cortex-M55), addressed directly as tiku_cpu_common.c
 * addresses SysTick.  SysTick reloads every kernel tick, too short a span to
 * time a program of up to milliseconds in one read, so the bench uses the
 * 32-bit DWT counter and calibrates its rate against the SysTick delay. */
#define TIKU_DWT_CTRL    (*(volatile uint32_t *)0xE0001000UL)
#define TIKU_DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004UL)
#define TIKU_SCB_DEMCR   (*(volatile uint32_t *)0xE000EDFCUL)

uint8_t tiku_mem_arch_nvm_bench(tiku_mem_nvm_bench_row_t *rows, uint8_t max,
                                unsigned long *dwt_hz_out)
{
    static const uint16_t sizes[] = { 16U, 256U, 4096U, 32768U };
    const uint8_t   nsizes    = (uint8_t)(sizeof(sizes) / sizeof(sizes[0]));
    const uintptr_t mirror    = (uintptr_t)__tiku_nvm_mram_start;
    const size_t    bench_off = TIKU_NVM_MRAM_BYTES / 2U;  /* upper half */
    uint32_t primask, c0, c1;
    uint8_t  i, r, count = 0U;

    if (dwt_hz_out) { *dwt_hz_out = 0UL; }
    if (rows == NULL || max == 0U) { return 0U; }
    if (bench_off < TIKU_NVM_MIRROR_HDR_BYTES ||
        uninit_bytes() > bench_off - TIKU_NVM_MIRROR_HDR_BYTES) { return 0U; }

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

        if (sizes[i] > sizeof g_nvm_snap ||
            sizes[i] > TIKU_NVM_MRAM_BYTES - bench_off) {
            continue;
        }
        for (w = 0U; w < words; w++) {
            g_nvm_snap[w] = 0xA5A50000UL ^ (uint32_t)(w * 2654435761UL);
        }
        /* g_nvm_snap is cached SSRAM on the M55: clean the source lines so the
         * bootrom reads the pattern written above, not a stale cache image. */
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

    /* Drop cached copies of the clobbered scratch.  The live image in the
     * bottom half is untouched, so durable state needs no flush or restore. */
    tiku_cpu_dcache_invalidate((const void *)(mirror + bench_off),
                               TIKU_NVM_MRAM_BYTES - bench_off);
    return count;
}
