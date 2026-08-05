/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mram_arch.c - RA8P1 code-MRAM programming.
 *
 * The sequence is the manual's (UM 60.4.2 and usage note 6): store, barrier,
 * flush, wait for the sequencer -- then read the error flags, never before.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mram_arch.h"
#include "tiku_ra8p1_regs.h"

/** @brief Bound on every sequencer wait; ~ms at any clock this port runs. */
#define MRAM_SPINS  2000000UL

/**
 * @brief Spin until @p mask reaches @p want in MRCPS, or the budget expires.
 *
 * Bounded because a stalled program sequencer must be reportable: hanging
 * here would turn a recoverable NVM fault into a dead board.
 *
 * @param mask  Bits to test
 * @param want  Value those bits must reach
 * @return 1 when the condition was met, 0 on timeout
 */
static int mrcps_wait(uint8_t mask, uint8_t want)
{
    unsigned long spins;

    for (spins = MRAM_SPINS; spins != 0UL; spins--) {
        if ((TIKU_REG8(RA8P1_MRCPS) & mask) == want) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Read and clear the two programming error flags.
 *
 * @return TIKU_RA8P1_MRAM_OK, or the error the hardware latched
 */
static int mram_take_errors(void)
{
    uint8_t st = TIKU_REG8(RA8P1_MRCPS);
    int rc = TIKU_RA8P1_MRAM_OK;

    if (st & RA8P1_MRCPS_PRGERRC) { rc = TIKU_RA8P1_MRAM_ERR_PROG; }
    if (st & RA8P1_MRCPS_ECCERRC) { rc = TIKU_RA8P1_MRAM_ERR_ECC; }
    if (rc != TIKU_RA8P1_MRAM_OK) {
        /* Write-0-to-clear, and the manual asks for a re-check because the
         * clear races the sequencer setting it again. */
        TIKU_REG8(RA8P1_MRCPS) = 0U;
        (void)TIKU_REG8(RA8P1_MRCPS);
    }
    return rc;
}

void tiku_ra8p1_mram_program_enable(int on)
{
    if (on) {
        /* MRCPSEN first: BPCN1 is ignored while it is 0, so the reverse
         * order silently leaves block protection standing. */
        TIKU_REG16(RA8P1_MRCPC1) = (uint16_t)(RA8P1_MRCPC1_KEY |
                                              RA8P1_MRCPC1_MRCPSEN);
        TIKU_REG16(RA8P1_MRCBPROT1) = (uint16_t)(RA8P1_MRCBPROT1_KEY |
                                                 RA8P1_MRCBPROT1_BPCN1);
    } else {
        TIKU_REG16(RA8P1_MRCBPROT1) = (uint16_t)RA8P1_MRCBPROT1_KEY;
        TIKU_REG16(RA8P1_MRCPC1) = (uint16_t)RA8P1_MRCPC1_KEY;
    }
    __asm__ volatile ("dsb" ::: "memory");
}

int tiku_ra8p1_mram_flush(void)
{
    /* MRCFL is only writable with something buffered, so ABUFEMP still gates
     * the write.  It is NOT a licence to return early, though: reads are
     * served from the write buffer -- measured, a store reads back with its
     * new value before any flush -- so nothing the CPU can observe separates
     * "buffered" from "in the array".  The sequencer wait and the error check
     * therefore run on both paths; skipping them on ABUFEMP would report a
     * commit that had not happened and would drop a program error entirely.
     */
    /*
     * The barrier goes BEFORE the status read, not before the MRCFL write.
     *
     * The store that filled the buffer is posted; a read of MRCPS -- a
     * different peripheral address -- can overtake it, observe ABUFEMP=1 for
     * a buffer that is about to be written, and skip the commit entirely.
     * The write is then lost with rc=0 reported.  This is how it presented:
     * long store runs committed fine and single-word cell writes silently did
     * not, because only the short ones lose the race.
     */
    __asm__ volatile ("dsb" ::: "memory");

    if (!(TIKU_REG8(RA8P1_MRCPS) & RA8P1_MRCPS_ABUFEMP)) {
        TIKU_REG16(RA8P1_MRCFLR) = (uint16_t)(RA8P1_MRCFLR_KEY |
                                              RA8P1_MRCFLR_MRCFL);
        if (!mrcps_wait(RA8P1_MRCPS_ABUFEMP, RA8P1_MRCPS_ABUFEMP)) {
            return TIKU_RA8P1_MRAM_ERR_BUSY;
        }
    }
    if (!mrcps_wait(RA8P1_MRCPS_PRGBSYC, 0U)) {
        return TIKU_RA8P1_MRAM_ERR_BUSY;
    }
    return mram_take_errors();
}

int tiku_ra8p1_mram_write(void *dst, const void *src, size_t len)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (dst == NULL || src == NULL || len == 0U) {
        return TIKU_RA8P1_MRAM_OK;
    }

    if (!mrcps_wait(RA8P1_MRCPS_PRGBSYC, 0U)) {
        return TIKU_RA8P1_MRAM_ERR_BUSY;
    }

    /*
     * Plain stores, no per-byte status poll.  An earlier version checked
     * ABUFFULL before every byte, which cost ~22 cycles each and made a
     * 32-byte line 17.3 us instead of ~1 us -- eight times the write in
     * bookkeeping.  The bus stalls a store the buffer cannot take, which is
     * the check that was being duplicated in software.
     *
     * Word stores where the alignment allows, since the buffer takes them
     * whole: measured 174 counts for a granule against 468 byte-wise.
     */
    while ((len != 0U) && (((uintptr_t)d & 3U) != 0U)) {
        *d++ = *s++;
        len--;
    }
    if (((uintptr_t)s & 3U) == 0U) {
        while (len >= 4U) {
            *(volatile uint32_t *)(void *)d = *(const uint32_t *)(const void *)s;
            d += 4;
            s += 4;
            len -= 4U;
        }
    }
    while (len-- > 0U) {
        *d++ = *s++;
    }

    return tiku_ra8p1_mram_flush();
}

void tiku_ra8p1_mram_high_speed(int on)
{
    TIKU_REG8(RA8P1_MRPSC) = on ? (uint8_t)RA8P1_MRPSC_MHSPEN : 0U;
}
