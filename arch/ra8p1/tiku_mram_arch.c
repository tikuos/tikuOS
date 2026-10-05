/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mram_arch.c - RA8P1 code-MRAM programming.
 *
 * Follows UM 60.4.2 and usage note 6: store, barrier, flush, wait for the
 * sequencer, then read the error flags.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mram_arch.h"
#include "tiku_ra8p1_regs.h"

/** @brief Iteration bound on every MRCPS wait in this file. */
#define MRAM_SPINS  2000000UL

/**
 * @brief Spin until @p mask reaches @p want in MRCPS, or the budget expires.
 *
 * Gives up after MRAM_SPINS reads, so a stalled sequencer becomes an error
 * return.
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
 * @return TIKU_RA8P1_MRAM_OK, TIKU_RA8P1_MRAM_ERR_PROG or
 *         TIKU_RA8P1_MRAM_ERR_ECC; ECC when both flags are set
 */
static int mram_take_errors(void)
{
    uint8_t st = TIKU_REG8(RA8P1_MRCPS);
    int rc = TIKU_RA8P1_MRAM_OK;

    if (st & RA8P1_MRCPS_PRGERRC) { rc = TIKU_RA8P1_MRAM_ERR_PROG; }
    if (st & RA8P1_MRCPS_ECCERRC) { rc = TIKU_RA8P1_MRAM_ERR_ECC; }
    if (rc != TIKU_RA8P1_MRAM_OK) {
        /* The flags clear on a write of 0.  The manual asks for a re-check
         * because the clear can race the sequencer setting a flag again; the
         * value read back here is discarded. */
        TIKU_REG8(RA8P1_MRCPS) = 0U;
        (void)TIKU_REG8(RA8P1_MRCPS);
    }
    return rc;
}

void tiku_ra8p1_mram_program_enable(int on)
{
    if (on) {
        /* MRCPSEN is written first: BPCN1 is ignored while MRCPSEN is 0,
         * and the reverse order leaves block protection on with no error. */
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
    /*
     * The last store to MRAM is posted, and a read of MRCPS can overtake it
     * and see ABUFEMP=1 for a buffer about to be written.  The DSB before
     * the read keeps the two in order.
     *
     * MRCFL takes a flush only while the buffer holds data, so ABUFEMP gates
     * the MRCFLR write.  The PRGBSYC wait and the error check run on both
     * paths: a buffer that committed by itself, on filling or on a store
     * leaving its 32-byte line, can still be programming.  Reads are served
     * from the program buffer, so reading the data back does not show that
     * it was committed.
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
     * The bus stalls a store the program buffer cannot take, so the copy
     * does not poll ABUFFULL.  Bytes go first until the destination is
     * word-aligned, then words when the source is aligned too, then bytes.
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
