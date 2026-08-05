/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.c - RA8P1 DMAC channel 0, software-triggered memcpy.
 *
 * The transfer is bracketed by cache maintenance, which on this part is not
 * optional: the DMAC is a bus master that neither sees nor is seen by the
 * Cortex-M85 D-cache.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dma_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_cache_arch.h"

/** @brief The one channel this port drives. */
#define DMA_CH  0U

static tiku_dma_done_cb_t dma_cb;
static void              *dma_ctx;
static void              *dma_dst;
static size_t             dma_bytes;
static uint8_t            dma_ready;

void tiku_dma_arch_init(void)
{
    if (dma_ready) {
        return;
    }

    /* Ungate the block, then activate it: no channel runs while DMAST.DMST is
     * 0, and the module-stop bit must be cleared before any DMAC register
     * responds at all. */
    TIKU_REG32(RA8P1_MSTPCRA) &= ~RA8P1_MSTPA_DMAC;
    (void)TIKU_REG32(RA8P1_MSTPCRA);
    TIKU_REG8(RA8P1_DMAC_DMAST) = RA8P1_DMAC_DMAST_DMST;

    TIKU_REG8(RA8P1_DMAC_DMCNT(DMA_CH)) = 0U;      /* channel parked */

    /* Link the transfer-end event onto its slot, but leave the NVIC line
     * masked: an unmasked slot with nothing armed is how a stale pend fires a
     * callback that was never scheduled. */
    TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_DMAC0)) = RA8P1_EVENT_DMAC0_INT;
    TIKU_REG32(RA8P1_NVIC_ICER(RA8P1_ICU_SLOT_DMAC0 >> 5)) =
        1UL << (RA8P1_ICU_SLOT_DMAC0 & 0x1FU);

    __asm__ volatile ("dsb" ::: "memory");
    dma_ready = 1U;
}

int tiku_dma_arch_busy(void)
{
    if (!dma_ready) {
        return 0;
    }
    return (TIKU_REG8(RA8P1_DMAC_DMCNT(DMA_CH)) & RA8P1_DMCNT_DTE) ? 1 : 0;
}

int tiku_dma_arch_memcpy(void *dst, const void *src, size_t word_cnt,
                         tiku_dma_done_cb_t on_done, void *ctx)
{
    if (!dma_ready) {
        return TIKU_DMA_ERR_NOT_READY;
    }
    if (dst == NULL || src == NULL || word_cnt == 0U || word_cnt > 65535U) {
        return TIKU_DMA_ERR_INVALID;
    }
    if ((((uintptr_t)dst | (uintptr_t)src) & 3U) != 0U) {
        return TIKU_DMA_ERR_INVALID;
    }
    /* Overlapping ranges: the DMAC would race itself, and the caller almost
     * certainly meant two buffers. */
    {
        uintptr_t s = (uintptr_t)src;
        uintptr_t d = (uintptr_t)dst;
        size_t    n = word_cnt * 4U;

        if ((s < d + n) && (d < s + n)) {
            return TIKU_DMA_ERR_INVALID;
        }
    }
    if (tiku_dma_arch_busy()) {
        return TIKU_DMA_ERR_BUSY;
    }

    dma_cb    = on_done;
    dma_ctx   = ctx;
    dma_dst   = dst;
    dma_bytes = word_cnt * 4U;

    /*
     * Cache discipline, and the order is load-bearing.  Two wrong versions
     * came first, each failing in the opposite direction:
     *
     *   invalidate the destination only AFTER the transfer -- discards dirty
     *   data belonging to whatever shares the first and last lines (it
     *   silently reverted the test harness's own assertion counter);
     *
     *   clean-and-invalidate AFTER the transfer -- writes the CPU's stale
     *   pre-transfer contents back OVER what the DMAC just wrote.
     *
     * The destination must be cleaned-and-invalidated BEFORE the transfer, so
     * no dirty line survives to land on top of the DMA data, and invalidated
     * after, to drop anything fetched while it ran.  The source is cleaned so
     * the DMAC reads what the CPU wrote rather than what RAM held.
     *
     * Both buffers must be cache-line aligned; a shared edge line makes no
     * maintenance sequence safe.
     */
    tiku_ra8p1_dcache_clean(src, dma_bytes);
    tiku_ra8p1_dcache_clean_invalidate(dst, dma_bytes);

    TIKU_REG32(RA8P1_DMAC_DMSAR(DMA_CH)) = (uint32_t)(uintptr_t)src;
    TIKU_REG32(RA8P1_DMAC_DMDAR(DMA_CH)) = (uint32_t)(uintptr_t)dst;
    TIKU_REG32(RA8P1_DMAC_DMCRA(DMA_CH)) = (uint32_t)word_cnt;
    TIKU_REG16(RA8P1_DMAC_DMTMD(DMA_CH)) = (uint16_t)(RA8P1_DMTMD_MD_NORMAL |
                                                      RA8P1_DMTMD_SZ_32);
    TIKU_REG16(RA8P1_DMAC_DMAMD(DMA_CH)) = (uint16_t)(RA8P1_DMAMD_SM_INC |
                                                      RA8P1_DMAMD_DM_INC);
    TIKU_REG8(RA8P1_DMAC_DMSTS(DMA_CH)) = 0U;      /* clear stale flags */
    TIKU_REG8(RA8P1_DMAC_DMINT(DMA_CH)) =
        (on_done != NULL) ? RA8P1_DMINT_DTIE : 0U;

    if (on_done != NULL) {
        TIKU_REG32(RA8P1_NVIC_ICPR(RA8P1_ICU_SLOT_DMAC0 >> 5)) =
            1UL << (RA8P1_ICU_SLOT_DMAC0 & 0x1FU);
        TIKU_REG32(RA8P1_NVIC_ISER(RA8P1_ICU_SLOT_DMAC0 >> 5)) =
            1UL << (RA8P1_ICU_SLOT_DMAC0 & 0x1FU);
    }

    TIKU_REG8(RA8P1_DMAC_DMCNT(DMA_CH)) = RA8P1_DMCNT_DTE;
    __asm__ volatile ("dsb" ::: "memory");

    /*
     * Software trigger, and CLRS matters: with CLRS=0 the hardware clears
     * SWREQ once the transfer STARTS, which moves exactly one unit and stops.
     * The first version did that and the destination held one word of the
     * source with the channel still enabled -- "busy never clears, buffers
     * do not match".  CLRS=1 keeps the request asserted so the DMAC runs the
     * count down to zero.
     */
    TIKU_REG8(RA8P1_DMAC_DMREQ(DMA_CH)) = (uint8_t)(RA8P1_DMREQ_CLRS |
                                                    RA8P1_DMREQ_SWREQ);
    return TIKU_DMA_OK;
}

int tiku_dma_arch_abort(void)
{
    if (!dma_ready) {
        return TIKU_DMA_ERR_NOT_READY;
    }
    TIKU_REG8(RA8P1_DMAC_DMCNT(DMA_CH)) = 0U;
    TIKU_REG8(RA8P1_DMAC_DMREQ(DMA_CH)) = 0U;
    TIKU_REG8(RA8P1_DMAC_DMINT(DMA_CH)) = 0U;
    dma_cb = NULL;
    __asm__ volatile ("dsb" ::: "memory");
    return TIKU_DMA_OK;
}

void tiku_ra8p1_dmac0_handler(void)
{
    tiku_dma_done_cb_t cb = dma_cb;
    void *ctx = dma_ctx;

    TIKU_REG8(RA8P1_DMAC_DMSTS(DMA_CH)) = 0U;      /* clear DTIF */
    TIKU_REG8(RA8P1_DMAC_DMREQ(DMA_CH)) = 0U;      /* drop the held request */

    /* Both halves of the ack, as every ICU slot on this port needs: the
     * IELSR status bit AND the NVIC pending bit, or the slot re-fires the
     * instant it is next unmasked. */
    TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_DMAC0)) &= ~RA8P1_ICU_IELSR_IR;
    (void)TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_DMAC0));
    TIKU_REG32(RA8P1_NVIC_ICPR(RA8P1_ICU_SLOT_DMAC0 >> 5)) =
        1UL << (RA8P1_ICU_SLOT_DMAC0 & 0x1FU);
    __asm__ volatile ("dsb" ::: "memory");

    /* Drop anything the core fetched into these lines while the DMAC was
     * writing them.  Safe as a plain invalidate because the pre-transfer
     * clean already wrote back everything that shared an edge line. */
    if (dma_dst != NULL && dma_bytes != 0U) {
        tiku_ra8p1_dcache_invalidate(dma_dst, dma_bytes);
    }

    dma_cb = NULL;
    if (cb != NULL) {
        cb(ctx);
    }
}
